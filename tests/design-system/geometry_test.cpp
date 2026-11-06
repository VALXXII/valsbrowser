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
    // Toolbar reference gap derived from the current toolbar: back {6,38}, forward {44,76}.
    const float toolbarGap=44-38;
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
    Require(Near(Focus::color,1)&&Near(Focus::opacity,.10f),"focus ring is white at 10% alpha");
    std::puts("PASS: 6 DIP tab/toolbar gap, contiguous hit regions, overflow capacity, equal close insets, 1 DIP white 10% focus tokens.");
    return 0;
}
