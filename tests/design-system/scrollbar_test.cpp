// Overlay scrollbar (Paper V3 "Scrollbar minimale"): values, geometry from real scroll
// metrics, hit testing, raster (antialiased fill + outer ring) and motion timings.
#include "../../src/viewport-scrollbar.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace NativeChrome::Scrollbar;
namespace M = NativeChrome::Motion;
namespace {
int checks=0;
void Require(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
bool Near(float a,float b,float e=.001f){return std::fabs(a-b)<e;}
M::Time T(int ms){return M::Time{}+M::Milliseconds(ms);}
Metrics Page(float y,float viewH,float contentH,float viewW=1176,float contentW=1176){
    Metrics m;m.x=0;m.y=y;m.viewW=viewW;m.viewH=viewH;m.contentW=contentW;m.contentH=contentH;m.canX=m.canY=true;m.valid=true;return m;}
float Alpha(uint32_t p){return (p>>24)/255.f;}
}

int main(){
    // Paper values (#1717178F, #FFFFFF29 ring / #171717CC, #FFFFFF3D on hover).
    Require(edgeInset==2&&thickness==6&&hoverThickness==8&&radius==3&&hoverRadius==4&&ring==1,"Paper geometry 2/6/8/3/4/1 DIP");
    Require(fillAlpha==0x8F&&hoverFillAlpha==0xCC&&ringAlpha==0x29&&hoverRingAlpha==0x3D&&fillRgb==0x171717&&ringRgb==0xFFFFFF,"Paper colours");
    Require(fadeInMs==100&&holdMs==800&&fadeOutMs==200&&widenMs==120,"Paper timings");

    // Geometry from the real page, not the 182 DIP mock-up: 729 DIP viewport (815 window).
    for(float scale:{1.f,1.25f,1.75f}){
        const float w=1176*scale,h=729*scale;
        auto top=Compute(Page(0,729,729*4),w,h,scale,8);
        Require(top.v.present&&!top.h.present,"vertical only when content is taller");
        Require(Near(top.v.trackStart,4*scale),"top inset 4 DIP (square top corners)");
        Require(Near(top.v.trackStart+top.v.trackLength,h-8*scale),"bottom inset keeps the 8 DIP corner");
        Require(Near(top.v.thumbLength,top.v.trackLength/4),"thumb length = track x view/content");
        Require(Near(top.v.thumbStart,top.v.trackStart),"thumb at the top at scrollTop 0");
        auto end=Compute(Page(729*3,729,729*4),w,h,scale,8);
        Require(Near(end.v.thumbStart+end.v.thumbLength,end.v.trackStart+end.v.trackLength),"thumb at the end at max scroll");
        auto mid=Compute(Page(729*1.5f,729,729*4),w,h,scale,8);
        Require(Near(mid.v.thumbStart-mid.v.trackStart,mid.v.Travel()/2,.01f),"thumb half way at half scroll");
        Require(Near(ScrollFor(mid.v,mid.v.thumbStart),729*1.5f,.05f),"drag mapping inverts the position");
        Require(Near(Outer(mid,true),w-2*scale),"outer edge 2 DIP from the right");
        auto huge=Compute(Page(0,729,729*1000),w,h,scale,8);
        Require(Near(huge.v.thumbLength,minLength*scale),"long pages keep a grabbable handle");
        auto max=Compute(Page(0,729,729*4),w,h,scale,0);
        Require(Near(max.v.trackStart+max.v.trackLength,h-4*scale),"maximised (square corner): 4 DIP bottom inset");
        auto both=Compute(Page(0,729,2000,1176,3000),w,h,scale,8);
        Require(both.v.present&&both.h.present,"both axes when both overflow");
        Require(Near(both.v.trackStart+both.v.trackLength,h-crossReserve*scale)&&Near(both.h.trackStart+both.h.trackLength,w-crossReserve*scale),"tracks stop short of each other");
        Require(Near(both.h.trackStart,8*scale),"horizontal track clears the bottom-left corner");
    }
    Require(!Compute(Page(0,729,729),1176,729,1,8).v.present,"no handle without overflow");
    {auto m=Page(0,729,2000);m.canY=false;Require(!Compute(m,1176,729,1,8).v.present,"no handle when the root overflow is hidden");}
    {Metrics m;Require(!Compute(m,1176,729,1,8).v.present,"no handle before the page reports");}
    {auto m=Page(0,729,2000,1000,3000);m.rtl=true;m.x=-2000;Require(Near(m.LeftX(),0),"RTL scrollLeft -range is the left edge");m.x=0;Require(Near(m.LeftX(),2000),"RTL scrollLeft 0 is the right edge");}

    // Hit testing: strip along the track reveals, the handle takes hover/drag.
    {auto l=Compute(Page(0,729,729*4),1176,729,1,8);
     Require(HitTest(l,1170,l.v.thumbStart+5).handle==Vertical,"pointer on the handle");
     Require(HitTest(l,1170,l.v.thumbStart+l.v.thumbLength+50).band==Vertical&&HitTest(l,1170,l.v.thumbStart+l.v.thumbLength+50).handle==None,"pointer on the track strip");
     Require(HitTest(l,1176-12-1,l.v.thumbStart+5).band==None,"left of the 12 DIP strip goes to the page");
     Require(HitTest(l,1170,2).band==None,"above the track goes to the page");}

    // Raster at 100 % and 125 %: fill alpha, ring outside, transparent beyond, right edge fixed.
    for(float scale:{1.f,1.25f}){
        const float outer=1174*scale;const int len=(int)std::lround(100*scale);
        for(float t:{0.f,1.f}){
            auto tile=Raster(outer,len,scale,t,true);const int w=tile.across;
            Require(w>0&&tile.along>len&&(int)tile.pixels.size()==w*tile.along,"tile size");
            auto at=[&](float x,int y){int i=(int)std::floor(x-tile.originAcross);return tile.pixels[(size_t)y*w+i];};
            const int mid=tile.along/2;const float th=Thick(t)*scale;
            const float fill=(t?hoverFillAlpha:fillAlpha)/255.f,rg=(t?hoverRingAlpha:ringAlpha)/255.f;
            Require(Near(Alpha(at(outer-th/2,mid)),fill,.01f),"handle centre is the Paper fill alpha");
            Require(((at(outer-th/2,mid)>>16)&255)==(uint32_t)std::lround(0x17*fill),"fill colour premultiplied #171717");
            if(scale==1){Require(Near(Alpha(at(outer+.5f,mid)),rg,.01f),"ring pixel right of the handle");
                         Require(Near(Alpha(at(outer-th-.5f,mid)),rg,.01f),"ring pixel left of the handle (widened to the left)");}
            Require(Alpha(at(outer+scale+1.4f,mid))==0,"nothing beyond the ring");
            Require(Alpha(tile.pixels[0])==0,"tile corner transparent (rounded)");
            int partial=0;for(int y=0;y<tile.along;++y){float a=Alpha(tile.pixels[(size_t)y*w+w/2]);if(a>0&&a<fill-.02f&&a!=rg)++partial;}
            Require(partial>=2,"rounded ends are antialiased");
        }
    }

    // Motion: 100 ms in, 800 ms hold after the last scroll, 200 ms out; 120 ms widen.
    {Motion m;Require(m.Sample(T(0)).opacity==0&&!m.NextWake(T(0)).delay,"rest: invisible and idle");
     m.Scrolled(T(0));Require(m.NextWake(T(0)).frames,"fade in requests frames");
     Require(m.Sample(T(50)).opacity>.4f&&m.Sample(T(50)).opacity<1,"fading in at 50 ms");
     Require(m.Sample(T(100)).opacity==1,"fully shown at 100 ms");
     m.Scrolled(T(300));m.Advance(T(1099));Require(m.Sample(T(1099)).opacity==1,"held 800 ms after the last scroll");
     auto wake=m.NextWake(T(1000));Require(wake.delay&&wake.delay->count()==100&&!wake.frames,"hold sleeps until the deadline");
     m.Advance(T(1100));Require(m.NextWake(T(1100)).frames,"fade out starts at 1100 ms");
     Require(m.Sample(T(1200)).opacity>0&&m.Sample(T(1200)).opacity<1,"fading out");
     Require(m.Sample(T(1300)).opacity==0,"gone after 200 ms");
     m.Advance(T(1300));Require(!m.NextWake(T(1300)).delay&&!m.Shown(T(1300)),"rest again, idle");
     m.Pointer(true,false,false,T(2000),false);Require(m.Shown(T(2000))&&m.Sample(T(2100)).opacity==1,"track strip reveals a resting handle");
     m.Advance(T(5000));Require(m.Sample(T(5000)).opacity==1,"held while the pointer is on the track");
     m.Pointer(true,true,false,T(5000),false);Require(m.Sample(T(5060)).hover>0&&m.Sample(T(5060)).hover<1&&m.Sample(T(5120)).hover==1,"widen 6->8 over 120 ms");
     m.Pointer(false,false,false,T(6000),false);m.Advance(T(6799));Require(m.Sample(T(6799)).opacity==1,"held 800 ms after the pointer leaves");
     m.Advance(T(6800));Require(m.Sample(T(7000)).opacity==0,"then fades out");}
    {Motion m;m.Scrolled(T(0));m.Pointer(true,true,false,T(0),true);
     Require(m.Sample(T(0)).hover==1,"reduced motion: width snaps");
     Require(m.Sample(T(50)).opacity>0&&m.Sample(T(50)).opacity<1,"reduced motion: opacity still fades");
     m.Reset();Require(m.Sample(T(60)).opacity==0&&m.Sample(T(60)).hover==0,"tab switch hides at once");}
    {Motion m;m.Pointer(true,true,true,T(0),false);m.Advance(T(10000));Require(m.Sample(T(10000)).opacity==1&&m.Sample(T(10000)).hover==1,"dragging keeps the handle wide and visible");}
    // Dark sites (comment c-l6gvbn, Paper S16 « D — site sombre »): the swapped handle.
    Require(darkFillRgb==0xFFFFFF&&darkRingRgb==0x171717,"dark-site handle: colours swapped, same alphas");
    Require(Over(0x000000,128,0xFFFFFF)==0x7F7F7F&&Over(0xFFFFFF,128,0x000000)==0x808080,"source-over: black 50 % on white, white 50 % on black");
    Require(Over(0x000000,64,Over(0x000000,128,0xFFFFFF))==0x5F5F5F,"source-over: nested translucent layers compose");
    // The light-page handle on a dark page was the reported defect: almost no contrast.
    Require(Contrast(Over(fillRgb,fillAlpha,0x121212),0x121212)<1.2,"defect reproduced: #171717 56 % on #121212 is near-invisible");
    Require(Contrast(Over(darkFillRgb,fillAlpha,0x121212),0x121212)>3,"swapped handle reads on #121212 (WCAG 1.4.11, 3:1)");
    Require(Contrast(Over(fillRgb,fillAlpha,0xFFFFFF),0xFFFFFF)>3,"light-page handle still reads on white (WCAG 1.4.11, 3:1)");
    for(int dark:{0x121212,0x171717,0x0D1117,0x1E1E1E,0x202124,0x000000,0x3A3A3A})Require(DarkSurface(dark),"dark surfaces (canvas, Paper, GitHub, editors, black) take the swapped handle");
    for(int light:{0xFFFFFF,0xF5F5F5,0xFAFAF7,0xE8EAED,0xC0C0C0})Require(!DarkSurface(light),"light surfaces keep the validated handle");
    Require(!DarkSurface(-1)&&!DarkSurface(0x1000000)&&!DarkSurface(-2),"unknown or invalid surface keeps the default handle");
    // Mid greys: whichever handle has the higher contrast, so the choice never makes it worse.
    for(int g=0;g<=255;g+=5){const int bg=(g<<16)|(g<<8)|g;
        const double chosen=Contrast(Over(DarkSurface(bg)?darkFillRgb:fillRgb,fillAlpha,(uint32_t)bg),(uint32_t)bg);
        const double other=Contrast(Over(DarkSurface(bg)?fillRgb:darkFillRgb,fillAlpha,(uint32_t)bg),(uint32_t)bg);
        Require(chosen>=other,"the chosen handle is the more visible one at every grey");}
    // Raster: same geometry and alphas, colours swapped.
    {const float scale=1.25f;const float outer=100*scale;const int len=(int)(80*scale);
     for(float t:{0.f,1.f}){auto a=Raster(outer,len,scale,t,true),b=Raster(outer,len,scale,t,true,true);
        Require(a.across==b.across&&a.along==b.along&&a.originAcross==b.originAcross&&a.pixels.size()==b.pixels.size(),"dark raster: same tile");
        bool sameAlpha=true;for(size_t i=0;i<a.pixels.size();++i)if((a.pixels[i]>>24)!=(b.pixels[i]>>24))sameAlpha=false;Require(sameAlpha,"dark raster: same coverage and alpha per pixel");
        const uint32_t mid=b.pixels[(size_t)(b.along/2)*b.across+(size_t)((outer-Thick(t)*scale/2)-b.originAcross)];
        const int al=mid>>24,r=(mid>>16)&255;Require(al>0&&std::abs(r-al)<=1,"dark raster: body is white (premultiplied r == alpha)");
        const uint32_t lightMid=a.pixels[(size_t)(a.along/2)*a.across+(size_t)((outer-Thick(t)*scale/2)-a.originAcross)];
        Require(((lightMid>>16)&255)<((mid>>16)&255),"light raster body stays #171717");}}
    // Page message: ten fields (older script) or twelve (with the two track surfaces), strictly.
    {Metrics m;bool sc=false;
     Require(ParseMessage(L"lisiere-scrollbar 1 0 10 800 400 800 2400 1 1 0",m,sc)&&sc&&m.valid&&m.surfaceV==-1&&m.surfaceH==-1&&m.y==10&&m.contentH==2400,"ten fields: accepted, surfaces unknown");
     Require(ParseMessage(L"lisiere-scrollbar 0 0 0 800 400 1600 2400 1 1 0 1184274 16777215",m,sc)&&!sc&&m.surfaceV==0x121212&&m.surfaceH==0xFFFFFF&&m.contentW==1600,"twelve fields: surfaces per track");
     Require(!ParseMessage(L"lisiere-scrollbar 0 0 0 800 400 800 2400 1 1 0 5",m,sc),"eleven fields rejected");
     Require(!ParseMessage(L"lisiere-scrollbar 0 0 0 800 400 800 2400 1 1 0 1 2 3",m,sc),"thirteen fields rejected");
     Require(!ParseMessage(L"lisiere-scrollbar 0 0 0 800 400 800 2400 1 1 0 x",m,sc)&&!ParseMessage(L"lisiere-scrollbar 0 0 0 800 400 800 2400 1 1 0 16777216 0",m,sc)&&!ParseMessage(L"lisiere-scrollbar 0 0 0 800 400 800 2400 1 1 0 -2 0",m,sc),"junk or out-of-range surfaces rejected");
     Require(!ParseMessage(L"lisiere-scrollbar 0 nan 0 800 400 800 2400 1 1 0",m,sc)&&!ParseMessage(L"lisiere-scrollbar 0 0 0 -1 400 800 2400 1 1 0",m,sc)&&!ParseMessage(L"lisiere-scrollbar 0 0 0 800 400 800 1e9 1 1 0",m,sc),"non-finite, negative or huge metrics rejected");}
    std::printf("PASS: %d overlay scrollbar checks (Paper values, real-metric geometry at 100/125/175 %%, hit bands, AA raster with outer ring, 100/800/200/120 ms motion, reduced motion, dark-site palette by contrast, strict 10/12-field message).\n",checks);
    return 0;
}
