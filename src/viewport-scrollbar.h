#pragma once
// Minimal overlay scrollbar of the web viewport (Paper "Browser Design", V3
// "Scrollbar minimale": A défilement, B repos, C survol/glisser).
//
// Why the host draws it: WebView2 exposes one native scrollbar option,
// ICoreWebView2EnvironmentOptions8::ScrollBarStyle (DEFAULT or FLUENT_OVERLAY), with
// no colour, width, inset or timing. The handle is therefore a DirectComposition
// visual above the page (viewport-composition.h), sized from the page's real scroll
// metrics, and the page's own *root* scrollbar is hidden with one adopted stylesheet
// (`:root{scrollbar-width:none!important}`, top-level document only). Nested
// scrollers, iframes and the page DOM keep their native scrollbars and styles.
//
// Pure, header-only geometry, raster and motion: unit tested without a WebView.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "motion.h"

namespace NativeChrome { namespace Scrollbar {

// ---- Paper values (DIP, alpha as the exact 8-bit channel of the hex colour) -------
inline constexpr float edgeInset=2;                    // right (vertical) / bottom (horizontal)
inline constexpr float thickness=6,hoverThickness=8;   // widened to the left, outer edge fixed
inline constexpr float radius=thickness/2,hoverRadius=hoverThickness/2; // 3 / 4
inline constexpr float ring=1;                         // box-shadow: 0 0 0 1px (outside the handle)
inline constexpr uint32_t fillRgb=0x171717,ringRgb=0xFFFFFF;
inline constexpr int fillAlpha=0x8F,hoverFillAlpha=0xCC;  // #1717178F (56 %) / #171717CC (80 %)
inline constexpr int ringAlpha=0x29,hoverRingAlpha=0x3D;  // #FFFFFF29 (16 %) / #FFFFFF3D (24 %)
inline constexpr float endInset=4;                     // minimum inset at both track ends
inline constexpr float minLength=24;                   // shortest handle (not in Paper: grab size)
// Where the two tracks meet, each stops short of the other's widest footprint
// (inset + hover thickness + ring + 1 DIP gap).
inline constexpr float crossReserve=edgeInset+hoverThickness+ring+1;
// Pointer strip along each track: hovering it reveals the handle (so a resting,
// invisible handle stays discoverable); over the handle itself it widens.
inline constexpr float hitBand=edgeInset+hoverThickness+2;
inline constexpr int fadeInMs=100,holdMs=800,fadeOutMs=200,widenMs=120;
static_assert(radius==3&&hoverRadius==4,"Paper radii");
static_assert(crossReserve==12&&hitBand==12,"track reserve and pointer band");

// ---- Page metrics (CSS px, reported by the page script; ratios only are used) ------
struct Metrics {
    float x=0,y=0,viewW=0,viewH=0,contentW=0,contentH=0;
    bool canX=false,canY=false,rtl=false,valid=false;
    float RangeX()const{return std::max(0.f,contentW-viewW);}
    float RangeY()const{return std::max(0.f,contentH-viewH);}
    // Horizontal position from the left edge (RTL roots scroll 0 .. -range).
    float LeftX()const{return rtl?RangeX()+x:x;}
};

struct Axis {
    bool present=false;
    float trackStart=0,trackLength=0,thumbStart=0,thumbLength=0; // px along the axis
    float range=0;                                               // CSS px of scrollable range
    float Travel()const{return std::max(0.f,trackLength-thumbLength);}
};
struct Layout { Axis v,h; float scale=1,width=0,height=0; };

// Host size in physical px, DIP scale (dpi/96), viewport bottom-corner radius in DIP.
inline Layout Compute(const Metrics& m,float hostW,float hostH,float scale,float cornerDip){
    Layout out;out.scale=scale;out.width=hostW;out.height=hostH;
    if(!m.valid||hostW<=0||hostH<=0||scale<=0)return out;
    const bool wantY=m.canY&&m.contentH>m.viewH+.5f&&m.viewH>0;
    const bool wantX=m.canX&&m.contentW>m.viewW+.5f&&m.viewW>0;
    const float cornerEnd=std::max(endInset,cornerDip);
    auto fill=[&](Axis& a,float start,float end,float view,float content,float pos){
        a.trackStart=start;a.trackLength=end-start;a.range=std::max(0.f,content-view);
        const float minPx=minLength*scale;
        if(a.trackLength<minPx||a.range<=0){a=Axis{};return;}
        a.thumbLength=std::clamp(a.trackLength*view/content,minPx,a.trackLength);
        a.thumbStart=start+a.Travel()*std::clamp(pos/a.range,0.f,1.f);
        a.present=true;
    };
    if(wantY)fill(out.v,endInset*scale,hostH-(wantX?std::max(cornerDip,crossReserve):cornerEnd)*scale,m.viewH,m.contentH,m.y);
    if(wantX)fill(out.h,cornerEnd*scale,hostW-(out.v.present?std::max(cornerDip,crossReserve):cornerEnd)*scale,m.viewW,m.contentW,m.LeftX());
    return out;
}
// Thumb travel (px) -> scroll position (CSS px from the start), clamped.
inline float ScrollFor(const Axis& a,float thumbStart){
    if(!a.present||a.Travel()<=0)return 0;
    return std::clamp((thumbStart-a.trackStart)/a.Travel(),0.f,1.f)*a.range;
}

// Handle cross-axis edges (px) at hover mix t: the outer edge stays fixed.
inline float Thick(float t){return thickness+(hoverThickness-thickness)*t;}
inline float Outer(const Layout& l,bool vertical){return (vertical?l.width:l.height)-edgeInset*l.scale;}

enum Part { None=-1, Vertical=0, Horizontal=1 };
struct Hit { int band=None,handle=None; };
// (x,y) in host px. `band` is the pointer strip along a present track, `handle` the
// handle itself at its hover footprint (with the ring and the strip's outer margin).
inline Hit HitTest(const Layout& l,float x,float y){
    Hit hit;const float s=l.scale;
    if(l.v.present&&x>=l.width-hitBand*s&&x<l.width&&y>=l.v.trackStart&&y<l.v.trackStart+l.v.trackLength){
        hit.band=Vertical;if(y>=l.v.thumbStart-ring*s&&y<l.v.thumbStart+l.v.thumbLength+ring*s)hit.handle=Vertical;return hit;}
    if(l.h.present&&y>=l.height-hitBand*s&&y<l.height&&x>=l.h.trackStart&&x<l.h.trackStart+l.h.trackLength){
        hit.band=Horizontal;if(x>=l.h.thumbStart-ring*s&&x<l.h.thumbStart+l.h.thumbLength+ring*s)hit.handle=Horizontal;}
    return hit;
}

// ---- Raster ------------------------------------------------------------------------
// Premultiplied BGRA bitmap of one handle, `across` x `along` px (transposed for the
// horizontal handle), antialiased by signed distance. The handle sits with its outer
// edge at `outer` px from the tile's cross-axis origin; the ring is the 1 DIP band
// outside it (CSS box-shadow spread, hidden under the fill).
struct Tile { int across=0,along=0; float originAcross=0; std::vector<uint32_t> pixels; };
inline float RoundedDistance(float px,float py,float cx,float cy,float hx,float hy,float r){
    const float qx=std::fabs(px-cx)-(hx-r),qy=std::fabs(py-cy)-(hy-r);
    const float ox=std::max(qx,0.f),oy=std::max(qy,0.f);
    return std::sqrt(ox*ox+oy*oy)+std::min(std::max(qx,qy),0.f)-r;
}
inline uint32_t Premultiply(float alpha,uint32_t rgb){
    const float a=std::clamp(alpha,0.f,1.f);
    auto c=[a](uint32_t v){return (uint32_t)std::lround(v*a);};
    return ((uint32_t)std::lround(255*a)<<24)|(c((rgb>>16)&255)<<16)|(c((rgb>>8)&255)<<8)|c(rgb&255);
}
// outer: handle outer edge (px, host coordinates on the cross axis); length: handle
// length in whole px; t: hover mix; vertical selects the tile orientation.
inline Tile Raster(float outer,int length,float scale,float t,bool vertical){
    Tile tile;const float th=Thick(t)*scale,rg=ring*scale,r=std::min(th,(float)length)/2;
    const float inner=outer-th;tile.originAcross=std::floor(inner-rg-1);
    tile.across=(int)std::ceil(outer+rg+1)-(int)tile.originAcross;
    const int pad=(int)std::ceil(rg)+1;tile.along=length+2*pad;
    if(tile.across<=0||length<=0){tile.across=tile.along=0;return tile;}
    const float fa=(fillAlpha+(hoverFillAlpha-fillAlpha)*t)/255.f,ra=(ringAlpha+(hoverRingAlpha-ringAlpha)*t)/255.f;
    const float cx=(inner+outer)/2-tile.originAcross,hx=th/2,cy=tile.along/2.f,hy=length/2.f;
    const int w=vertical?tile.across:tile.along,h=vertical?tile.along:tile.across;
    tile.pixels.assign((size_t)w*h,0);
    for(int j=0;j<tile.along;++j)for(int i=0;i<tile.across;++i){
        const float d=RoundedDistance(i+.5f,j+.5f,cx,cy,hx,hy,r);
        const float body=std::clamp(.5f-d,0.f,1.f),halo=std::clamp(.5f-(d-rg),0.f,1.f)-body;
        const float a=fa*body+ra*halo;if(a<=0)continue;
        // Premultiplied sum of the two disjoint coverages.
        const float rr=(((fillRgb>>16)&255)*fa*body+((ringRgb>>16)&255)*ra*halo);
        const float gg=(((fillRgb>>8)&255)*fa*body+((ringRgb>>8)&255)*ra*halo);
        const float bb=((fillRgb&255)*fa*body+(ringRgb&255)*ra*halo);
        const uint32_t px=((uint32_t)std::lround(std::min(1.f,a)*255)<<24)|((uint32_t)std::lround(std::min(255.f,rr))<<16)|((uint32_t)std::lround(std::min(255.f,gg))<<8)|(uint32_t)std::lround(std::min(255.f,bb));
        tile.pixels[vertical?(size_t)j*w+i:(size_t)i*w+j]=px;
    }
    return tile;
}

// ---- Motion -------------------------------------------------------------------------
// Opacity: 100 ms ease-out in after a scroll, held 800 ms after the last one (or while
// the pointer is on the track or dragging), 200 ms ease-out out. Hover: 6 -> 8 DIP in
// 120 ms ease-out. Reduced motion keeps the opacity fades and snaps the width.
struct Visual { float opacity=0,hover=0; };
class Motion {
    NativeChrome::Motion::Tween opacity_,hover_;
    NativeChrome::Motion::Time hideAt_{};
    bool near_=false,hovered_=false,dragging_=false;
    bool Held()const{return near_||hovered_||dragging_;}
    void Reveal(NativeChrome::Motion::Time now){
        opacity_.Set(1,now,NativeChrome::Motion::Milliseconds(fadeInMs),false,NativeChrome::Motion::Curve::EaseOut);
        hideAt_=now+NativeChrome::Motion::Milliseconds(holdMs);
    }
public:
    void Scrolled(NativeChrome::Motion::Time now){Reveal(now);}
    // Pointer on the track strip / on the handle / dragging it.
    void Pointer(bool onTrack,bool hovered,bool dragging,NativeChrome::Motion::Time now,bool reduced){
        const bool wasHeld=Held();near_=onTrack;hovered_=hovered;dragging_=dragging;
        if(Held())Reveal(now);else if(wasHeld)hideAt_=now+NativeChrome::Motion::Milliseconds(holdMs);
        hover_.Set(hovered||dragging?1.f:0.f,now,NativeChrome::Motion::Milliseconds(widenMs),reduced,NativeChrome::Motion::Curve::EaseOut);
    }
    // Runs the hold timer: call before sampling.
    void Advance(NativeChrome::Motion::Time now){
        if(!Held()&&Target()>0&&now>=hideAt_)opacity_.Set(0,now,NativeChrome::Motion::Milliseconds(fadeOutMs),false,NativeChrome::Motion::Curve::EaseOut);
    }
    // Shown: the handle is (or is becoming) visible, so it takes pointer input.
    bool Shown(NativeChrome::Motion::Time now)const{return Target()>0||opacity_.Value(now)>0;}
    float Target()const{return opacity_.Value(NativeChrome::Motion::Time::max());}
    Visual Sample(NativeChrome::Motion::Time now)const{return {opacity_.Value(now),hover_.Value(now)};}
    NativeChrome::Motion::Wake NextWake(NativeChrome::Motion::Time now)const{
        if(opacity_.Moving(now)||hover_.Moving(now))return NativeChrome::Motion::Frame();
        if(!Held()&&Target()>0)return hideAt_<=now?NativeChrome::Motion::Frame():NativeChrome::Motion::Deadline(now,hideAt_);
        return {};
    }
    // Tab switch, navigation: hidden at once, no fade, no pointer state.
    void Reset(){opacity_.Snap(0);hover_.Snap(0);near_=hovered_=dragging_=false;hideAt_={};}
    void SettleHover(){hover_.Settle();}
};

}} // namespace NativeChrome::Scrollbar
