// Token-only fixture: tab-strip spacing, close-slot insets and focus tokens.
// Pure header test (no renderer, no WebView2). Build:
//   g++ -std=c++17 -Wall -Wextra -Werror tests/design-system/geometry_test.cpp -o geometry_test.exe
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../../src/design-system.h"
using namespace NativeChrome::Design;
static void Require(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<.001f;}
int main(){
    // Toolbar reference geometry, from the toolbar layout tokens (back {6,38}, forward {44,76}).
    const float backLeft=Space::sm,backRight=backLeft+Size::control,forwardLeft=backRight+Space::controlGap;
    const float toolbarGap=forwardLeft-backRight;
    Require(Near(Toolbar::backLeft,backLeft)&&Near(Toolbar::forwardLeft,forwardLeft),"toolbar column tokens match the toolbar layout");
    Require(Near(TabStrip::Surface(0).left,Toolbar::forwardLeft),"first tab surface starts on the forward button's left edge (44)");
    Require(Near(TabStrip::Region(0).left,Brand::Region().right),"brand region ends where the first tab hit region starts (38)");
    // Brand flower: 16 DIP, 32% opacity, centred on back-button centre X and tab-favicon centre Y.
    auto fav0=TabStrip::FaviconSlot(TabStrip::Surface(0));auto glyph=Brand::Glyph();
    Require(Near(Brand::size,Size::icon)&&Near(glyph.right-glyph.left,Brand::size)&&Near(glyph.bottom-glyph.top,Brand::size),"brand glyph 16x16");
    Require(Near(Brand::opacity,.32f),"brand opacity .32");
    Require(Near((glyph.left+glyph.right)/2,(backLeft+backRight)/2),"brand centre X == back button centre X (22)");
    Require(Near((glyph.top+glyph.bottom)/2,(fav0.top+fav0.bottom)/2),"brand centre Y == tab favicon centre Y (22)");
    auto brand=Brand::Region();Require(glyph.left>=brand.left&&glyph.right<=brand.right&&glyph.top>=brand.top&&glyph.bottom<=brand.bottom,"brand glyph inside its hit/focus region");
    Require(Near(Space::tabGap,toolbarGap)&&Near(Space::controlGap,toolbarGap),"tab gap == toolbar gap (6 DIP)");
    for(int i=0;i<6;++i){
        auto a=TabStrip::Surface(i),b=TabStrip::Surface(i+1);
        Require(Near(b.left-a.right,toolbarGap),"visible gap between adjacent tabs is 6 DIP");
        Require(Near(a.right-a.left,Size::tab),"visible tab width stays 128 DIP");
        auto r=TabStrip::Region(i),n=TabStrip::Region(i+1);
        Require(Near(r.right,n.left),"tab hit regions stay contiguous (no dead pointer zone between tabs)");
        Require(r.left<=a.left&&a.right<=r.right,"visible tab inside its hit region");
    }
    for(int shown=1;shown<6;++shown)Require(Near(TabStrip::NewTabLeft(shown)-TabStrip::Surface(shown-1).right,toolbarGap),"new-tab button keeps the same 6 DIP gap");
    // Overflow: everything shown at the computed capacity fits before the window controls.
    for(float width:{400.f,640.f,1180.f,1920.f,3840.f}){
        int c=TabStrip::Capacity(width);Require(c>=1,"capacity >= 1");
        if(width>=640)Require(TabStrip::NewTabLeft(c)+Size::control<=width-TabStrip::trailingReserve,"capacity never overlaps window controls");
        Require(TabStrip::NewTabLeft(c+1)+Size::control>width-TabStrip::trailingReserve||width<640,"capacity is maximal");
    }
    auto tab=TabStrip::Surface(0),close=TabStrip::CloseSlot(tab);
    Require(Near(close.right-close.left,24)&&Near(close.bottom-close.top,24),"close slot is 24x24");
    const float trailing=tab.right-close.right,top=close.top-tab.top,bottom=tab.bottom-close.bottom;
    Require(Near(trailing,top)&&Near(top,bottom)&&Near(top,4),"close slot inset equally (4 DIP) trailing/top/bottom");
    Require(Near((close.top+close.bottom)/2,(tab.top+tab.bottom)/2),"close slot vertically centred on the tab");
    auto fav=TabStrip::FaviconSlot(tab);
    Require(Near(fav.left-tab.left,(tab.right-close.right)+(Size::closeSlot-Size::icon)/2),"favicon leading inset mirrors close glyph trailing inset (8 DIP)");
    auto title=TabStrip::TitleSlot(tab);
    Require(Near(close.left-title.right,Space::xs)&&title.right>title.left,"title reserves the close slot permanently (no hover shift)");
    Require(Near(Size::focusStroke,1)&&Near(Focus::stroke,1),"focus ring is exactly 1 DIP");
    Require(Near(Focus::color,1)&&Near(Focus::pointerOpacity,.10f)&&Near(Focus::Opacity(false),.10f),"pointer focus ring is white at 10% alpha");
    Require(Near(Focus::keyboardOpacity,.50f)&&Near(Focus::Opacity(true),.50f),"keyboard focus ring is white at 50% alpha");
    std::puts("PASS: brand 16/.32 at back-centre X & favicon-centre Y, first tab at forward left, 6 DIP tab/toolbar gap, contiguous hit regions, overflow capacity, equal close insets, 1 DIP white focus tokens (pointer 10%, keyboard 50%).");
    return 0;
}
