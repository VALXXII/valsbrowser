#pragma once
// GPU composition of the web viewport (Paper "Lisière — prototype actuel V3",
// Outline > Viewbox): bottom corners rounded with antialiasing, top edge square.
//
// Every tab's WebView2 is hosted in visual mode (CompositionController): its content
// is a DirectComposition visual under one container bound to the native viewport
// host window (topmost layer of that HWND).
//
// Why a mask and not a rounded clip: IDCompositionRectangleClip accepts corner radii,
// but DCOMPOSITION_BORDER_MODE_SOFT is documented only for edges "not axis-aligned
// with integer coordinates", and on Windows 11 (measured, 120 DPI) its rounded
// corners come out binary: site or canvas, no intermediate pixel. The corners are
// therefore two small premultiplied-alpha surfaces composed above the web visuals:
// each pixel holds the canvas colour (#171717) at alpha = 1 - coverage of the rounded
// shape, coverage integrated on an 8x8 grid per pixel. The DWM blend then gives
// coverage * site + (1 - coverage) * canvas: a true antialiased edge for any page,
// at fractional radii (8 DIP = 10 px at 125 %, 14 px at 175 %), with no change to
// the page DOM. The container's clip stays a plain rectangle (no aliased radius).
//
// d3d11.dll and dcomp.dll are loaded at run time: no new link dependency for the
// browser or the native fixtures that include main.cpp; without them the caller
// keeps windowed hosting.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dcomp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace NativeViewport {

// Canvas alpha (0..1) of one mask pixel: 1 - the share of the pixel square inside the
// rounded shape. (i,j) are pixel indices in an n x n corner tile that ends at the
// host's bottom edge; the arc centre is r from the tile's outer side and bottom.
inline float MaskAlpha(int i,int j,int n,float r,bool right){
    const float cx=right?n-r:r,cy=n-r;int inside=0;
    for(int a=0;a<8;++a)for(int b=0;b<8;++b){
        const float x=i+(a+.5f)/8.f,y=j+(b+.5f)/8.f;
        const bool corner=(right?x>cx:x<cx)&&y>cy;
        if(!corner||(x-cx)*(x-cx)+(y-cy)*(y-cy)<=r*r)++inside;
    }
    return 1.f-inside/64.f;
}

class Compositor {
public:
    // canvas: the colour painted under the visual tree and around the host.
    bool Create(HWND host,COLORREF canvasColor){
        Destroy();hwnd=host;canvas=canvasColor;
        d3dModule=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        dcompModule=LoadLibraryExW(L"dcomp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto createD3D=d3dModule?reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(d3dModule,"D3D11CreateDevice")):nullptr;
        using CreateDComp=HRESULT(WINAPI*)(IDXGIDevice*,REFIID,void**);
        auto createDComp=dcompModule?reinterpret_cast<CreateDComp>(GetProcAddress(dcompModule,"DCompositionCreateDevice")):nullptr;
        if(!createD3D||!createDComp){Destroy();return false;}
        // The device only fills two tiny mask surfaces; WARP keeps it working on
        // machines whose GPU driver refuses a hardware device.
        HRESULT hr=E_FAIL;
        for(D3D_DRIVER_TYPE type:{D3D_DRIVER_TYPE_HARDWARE,D3D_DRIVER_TYPE_WARP}){
            hr=createD3D(nullptr,type,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,nullptr,&context);
            if(SUCCEEDED(hr))break;
        }
        IDXGIDevice* dxgi=nullptr;
        if(FAILED(hr)||FAILED(d3d->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi))){Destroy();return false;}
        hr=createDComp(dxgi,__uuidof(IDCompositionDevice),(void**)&device);dxgi->Release();
        if(FAILED(hr)||!device||FAILED(device->CreateTargetForHwnd(host,TRUE,&target))||
           FAILED(device->CreateVisual(&root))||FAILED(device->CreateVisual(&pages))||FAILED(device->CreateRectangleClip(&clip))||
           FAILED(pages->SetClip(clip))||FAILED(root->AddVisual(pages,TRUE,nullptr))||
           FAILED(device->CreateVisual(&mask[0]))||FAILED(device->CreateVisual(&mask[1]))||
           FAILED(root->AddVisual(mask[0],TRUE,pages))||FAILED(root->AddVisual(mask[1],TRUE,pages))||
           FAILED(target->SetRoot(root))||FAILED(device->Commit())){Destroy();return false;}
        // Overlay scrollbar handles (viewport-scrollbar.h): directly above the pages,
        // below the corner masks; opacity through an effect group (no re-raster per
        // fade frame). Optional: without them the page keeps its native scrollbar.
        bars=true;
        for(int k=0;k<2;++k)if(FAILED(device->CreateVisual(&bar[k]))||FAILED(device->CreateEffectGroup(&barEffect[k]))||
           FAILED(barEffect[k]->SetOpacity(0.f))||FAILED(bar[k]->SetEffect(barEffect[k]))||FAILED(root->AddVisual(bar[k],TRUE,pages)))bars=false;
        if(!bars)for(int k=0;k<2;++k){if(bar[k])root->RemoveVisual(bar[k]);Release(barEffect[k]);Release(bar[k]);}
        ++generation;device->Commit();
        return true;
    }
    bool Ready()const{return device&&pages&&clip;}
    // Overlay scrollbar layer available (see Create).
    bool Bars()const{return Ready()&&bars;}
    // Bumped on every successful Create: uploaded handle surfaces must be redrawn.
    unsigned Generation()const{return generation;}
    // Handle k (0 vertical, 1 horizontal): new premultiplied BGRA content, w x h px.
    bool SetBarContent(int k,const UINT32* pixels,int w,int h){
        if(!Bars()||k<0||k>1||w<=0||h<=0)return false;
        if(!barSurface[k]||barW[k]!=w||barH[k]!=h){
            bar[k]->SetContent(nullptr);Release(barSurface[k]);barW[k]=barH[k]=0;
            if(FAILED(device->CreateSurface(w,h,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&barSurface[k])))return false;
            barW[k]=w;barH[k]=h;
        }
        if(!Upload(barSurface[k],pixels,w,h))return false;
        return SUCCEEDED(bar[k]->SetContent(barSurface[k]));
    }
    // Handle k at (x,y) px in host coordinates, opacity 0..1. Commit() applies it.
    void PlaceBar(int k,float x,float y,float opacity){
        if(!Bars()||k<0||k>1)return;
        bar[k]->SetOffsetX(x);bar[k]->SetOffsetY(y);barEffect[k]->SetOpacity(std::clamp(opacity,0.f,1.f));
    }
    void HideBar(int k){if(Bars()&&k>=0&&k<2)barEffect[k]->SetOpacity(0.f);}
    // A device reset (driver update, TDR) loses the mask surfaces: the caller then
    // recreates the compositor and re-attaches every tab.
    bool Lost()const{
        if(!Ready())return false;BOOL valid=TRUE;
        return (d3d&&d3d->GetDeviceRemovedReason()!=S_OK)||(SUCCEEDED(device->CheckDeviceState(&valid))&&!valid);
    }
    HWND Host()const{return hwnd;}
    COLORREF Canvas()const{return canvas;}
    // A new visual for one WebView inside the clipped container, below the masks.
    // The caller releases it after RemoveChild.
    IDCompositionVisual* NewChild(){
        if(!Ready())return nullptr;IDCompositionVisual* v=nullptr;
        if(FAILED(device->CreateVisual(&v))||!v)return nullptr;
        if(FAILED(pages->AddVisual(v,TRUE,nullptr))){v->Release();return nullptr;}
        return v;
    }
    void RemoveChild(IDCompositionVisual* v){if(Ready()&&v){pages->RemoveVisual(v);device->Commit();}}
    void Commit(){if(device)device->Commit();}
    // Host client size and bottom-corner radius, in physical pixels. Radius 0
    // (maximised window) removes the masks: every corner is square.
    void Shape(float w,float h,float r){
        if(!Ready()||(w==width&&h==height&&r==radius))return;
        const bool redraw=r!=radius;width=w;height=h;radius=r;
        clip->SetLeft(0.f);clip->SetTop(0.f);clip->SetRight(w);clip->SetBottom(h);
        const int n=r>0?(int)std::ceil(r):0;
        if(redraw){for(int k=0;k<2;++k){mask[k]->SetContent(nullptr);if(surface[k]){surface[k]->Release();surface[k]=nullptr;}}
            bool drawn=n>0;if(n>0)for(int k=0;k<2;++k){if(DrawCorner(k==1,n,r))mask[k]->SetContent(surface[k]);else drawn=false;}
            masked=drawn;}
        mask[0]->SetOffsetX(0.f);mask[0]->SetOffsetY(h-n);
        mask[1]->SetOffsetX(w-n);mask[1]->SetOffsetY(h-n);
        device->Commit();
    }
    float Width()const{return width;}
    float Height()const{return height;}
    float Radius()const{return radius;}
    bool Masked()const{return masked;}
    // Pointer hit test against the visible shape: a pixel whose centre lies in a
    // masked corner shows the canvas, so it is outside the page.
    bool Contains(float x,float y)const{
        if(x<0||y<0||x>=width||y>=height)return false;
        if(radius<=0||y<height-radius||(x>=radius&&x<width-radius))return true;
        const float cx=x<radius?radius:width-radius,cy=height-radius,dx=x+.5f-cx,dy=y+.5f-cy;
        return dx*dx+dy*dy<=radius*radius;
    }
    void Destroy(){
        if(target)target->SetRoot(nullptr);
        if(device)device->Commit();
        for(int k=0;k<2;++k){Release(surface[k]);Release(mask[k]);Release(barSurface[k]);Release(barEffect[k]);Release(bar[k]);barW[k]=barH[k]=0;}bars=false;
        Release(clip);Release(pages);Release(root);Release(target);Release(device);Release(context);Release(d3d);
        if(dcompModule){FreeLibrary(dcompModule);dcompModule=nullptr;}
        if(d3dModule){FreeLibrary(d3dModule);d3dModule=nullptr;}
        width=height=radius=-1;masked=false;
    }
    ~Compositor(){Destroy();}
private:
    template<class T> static void Release(T*& p){if(p){p->Release();p=nullptr;}}
    bool DrawCorner(bool right,int n,float r){
        const int k=right?1:0;
        if(FAILED(device->CreateSurface(n,n,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&surface[k])))return false;
        std::vector<UINT32> pixels((size_t)n*n);
        for(int j=0;j<n;++j)for(int i=0;i<n;++i){
            const float a=MaskAlpha(i,j,n,r,right);
            auto channel=[a](BYTE c){return (UINT32)std::lround(c*a);};
            pixels[(size_t)j*n+i]=((UINT32)std::lround(255*a)<<24)|(channel(GetRValue(canvas))<<16)|(channel(GetGValue(canvas))<<8)|channel(GetBValue(canvas));
        }
        return Upload(surface[k],pixels.data(),n,n);
    }
    // Copies w x h premultiplied pixels into a whole DirectComposition surface.
    bool Upload(IDCompositionSurface* s,const UINT32* pixels,int w,int h){
        POINT at{};IDXGISurface* dxgi=nullptr;ID3D11Texture2D* texture=nullptr;
        if(FAILED(s->BeginDraw(nullptr,__uuidof(IDXGISurface),(void**)&dxgi,&at)))return false;
        bool ok=SUCCEEDED(dxgi->QueryInterface(__uuidof(ID3D11Texture2D),(void**)&texture));
        if(ok){const D3D11_BOX box{(UINT)at.x,(UINT)at.y,0,(UINT)(at.x+w),(UINT)(at.y+h),1};context->UpdateSubresource(texture,0,&box,pixels,w*4,0);texture->Release();}
        dxgi->Release();
        return SUCCEEDED(s->EndDraw())&&ok;
    }
    HWND hwnd=nullptr;COLORREF canvas=0;
    HMODULE d3dModule=nullptr,dcompModule=nullptr;
    ID3D11Device* d3d=nullptr;ID3D11DeviceContext* context=nullptr;
    IDCompositionDevice* device=nullptr;
    IDCompositionTarget* target=nullptr;
    IDCompositionVisual* root=nullptr;   // pages, then the two corner masks above them
    IDCompositionVisual* pages=nullptr;  // every tab's WebView visual, rectangle-clipped
    IDCompositionRectangleClip* clip=nullptr;
    IDCompositionVisual* mask[2]{};
    IDCompositionSurface* surface[2]{};
    IDCompositionVisual* bar[2]{};       // overlay scrollbar handles, above pages, below masks
    IDCompositionEffectGroup* barEffect[2]{};
    IDCompositionSurface* barSurface[2]{};int barW[2]{},barH[2]{};bool bars=false;
    unsigned generation=0;
    float width=-1,height=-1,radius=-1;bool masked=false;
};

}
