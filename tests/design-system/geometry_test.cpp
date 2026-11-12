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
    // Toolbar reference geometry (Paper nav pair): back {6,38}, 1 DIP hairline at 41 with
    // 3 DIP on each side, forward {45,77}, standalone Reload {83,115}, field at 121.
    const float backLeft=Space::sm,backRight=backLeft+Size::control,forwardLeft=backRight+3+1+3;
    const float toolbarGap=Space::controlGap;
    Require(Near(Toolbar::backLeft,backLeft)&&Near(Toolbar::backRight,backRight)&&Near(Toolbar::forwardLeft,forwardLeft),"toolbar column tokens match the Paper nav pair");
    Require(Near(Toolbar::separatorLeft,41)&&Near(Toolbar::separatorWidth,1)&&Near(Toolbar::separatorHeight,10)&&Near(Toolbar::separatorOpacity,.10f),"nav hairline 1 x 10 at x41, white 10%");
    Require(Near(Toolbar::separatorLeft-backRight,3)&&Near(forwardLeft-(Toolbar::separatorLeft+1),3),"3 DIP on each side of the hairline");
    Require(Near(Toolbar::innerRadius,3)&&Near(Radius::control,6),"pair corners: 6 outer, 3 inner");
    Require(Near(Toolbar::forwardRight,77)&&Near(Toolbar::reloadLeft,Toolbar::forwardRight+toolbarGap)&&Near(Toolbar::reloadRight-Toolbar::reloadLeft,Size::control),"standalone Reload 32 DIP, one control gap after forward (83..115)");
    Require(Near(Toolbar::addressLeft,Toolbar::reloadRight+toolbarGap)&&Near(Toolbar::addressLeft,121),"address field one control gap after Reload (121)");
    Require(Near(TabStrip::Surface(0).left,backRight+toolbarGap),"first tab surface starts one control gap after back (44)");
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
    // Adaptive strip: tabs shrink from 128 to the 32 DIP favicon-only square, then scroll
    // with a peeking, masked neighbour on whichever side still hides tabs.
    {
        auto few=TabStrip::Layout(1180,2,0,0);
        Require(Near(few.tab,Size::tab)&&!few.Overflow(),"few tabs keep the natural 128 DIP width");
        for(int i=0;i<2;++i){Require(Near(few.Surface(i).left,TabStrip::Surface(i).left)&&Near(few.Region(i).right,TabStrip::Region(i).right),"unscrolled natural strip matches the slot helpers");}
        Require(Near(few.NewTabLeft(),TabStrip::NewTabLeft(2)),"new-tab follows the last tab");
        float previous=Size::tab+1;
        for(float width:{560.f,800.f,1180.f,1920.f})for(int count=1;count<=80;++count)for(int selected:{0,count/2,count-1}){
            auto s=TabStrip::Layout(width,count,selected,1e6f*(selected%2));
            Require(s.tab>=Size::tabMin&&s.tab<=Size::tab&&Near(s.tab,std::floor(s.tab)),"tab width is a whole DIP between 32 and 128");
            Require(s.NewTabLeft()+Size::control<=width-TabStrip::trailingReserve+.001f,"new-tab never overlaps window controls");
            Require(Near(s.Surface(0).left+s.scroll,Toolbar::backRight+Space::tabGap),"first tab still starts at x44 before scrolling");
            for(int i=0;i+1<count&&i<4;++i){Require(Near(s.Surface(i+1).left-s.Surface(i).right,Space::tabGap),"6 DIP gap at every width");Require(Near(s.Region(i).right,s.Region(i+1).left),"contiguous regions at every width");}
            Require(s.Overflow()==(Near(s.tab,Size::tabMin)&&count*(Size::tabMin+Space::tabGap)>s.room),"overflow only once tabs are favicon-only");
            Require(s.Clear(selected),"selected tab is always fully clear of both fades");
            Require(s.scroll>=0&&s.scroll<=s.MaxScroll()&&Near(s.scroll,std::round(s.scroll)),"scroll is a whole DIP inside its range");
            Require(s.HiddenBefore()==(s.Surface(0).left<s.ViewLeft()-.001f),"left mask shown exactly when tabs hide before the view");
            Require(s.HiddenAfter()==(s.Surface(count-1).right>s.ViewRight()+.001f),"right mask shown exactly when tabs hide after the view");
            if(s.HiddenAfter()){int peek=-1;for(int i=0;i<count;++i)if(s.Visible(i)&&!s.Clear(i)&&s.Surface(i).right>s.ClearRight())peek=i;Require(peek>=0,"a hidden-side neighbour peeks under the right mask");}
            if(s.HiddenBefore()){int peek=-1;for(int i=0;i<count;++i)if(s.Visible(i)&&!s.Clear(i)&&s.Surface(i).left<s.ClearLeft())peek=i;Require(peek>=0,"a hidden-side neighbour peeks under the left mask");}
            if(s.Overflow()&&Near(s.scroll,0))Require([&]{for(int i=0;i<count;++i)if(s.Surface(i).left<s.ViewRight()&&s.Surface(i).right>s.ViewRight())return Near(s.ViewRight()-s.Surface(i).left,TabStrip::peek);return false;}(),"at the start the next hidden tab peeks exactly 16 DIP");
            if(s.Overflow()&&Near(s.scroll,s.MaxScroll()))Require([&]{for(int i=0;i<count;++i)if(s.Surface(i).left<s.ViewLeft()&&s.Surface(i).right>s.ViewLeft())return Near(s.Surface(i).right-s.ViewLeft(),TabStrip::peek);return false;}(),"at the end the previous hidden tab peeks exactly 16 DIP");
            if(width==1180&&selected==0){Require(s.tab<=previous,"tabs only shrink as tabs are added");previous=s.tab;}
        }
        auto icon=TabStrip::Layout(560,40,0,0);Require(!icon.Titled()&&!icon.HasClose(),"favicon-only tabs drop title and close");
        auto f=icon.Favicon(1),t=icon.Surface(1);Require(Near(f.left-t.left,t.right-f.right)&&Near(f.left-t.left,Space::md),"favicon centred with 8 DIP insets");
        auto swap=icon.CloseIn(icon.Surface(1));
        Require(icon.Closable(1,1)&&icon.SwapsClose(1,1)&&!icon.Closable(2,1),"favicon-only: only the selected tab swaps its favicon for close");
        Require(Near(swap.right-swap.left,Size::closeSlot)&&Near(swap.left-t.left,Size::closeInset)&&Near(t.right-swap.right,Size::closeInset)&&Near(swap.top-t.top,Size::closeInset),"swapped close is the 24 DIP slot centred with 4 DIP insets");
        auto narrowSwap=TabStrip::Layout(1180,12,3,0);auto ns=narrowSwap.CloseIn(narrowSwap.Surface(3)),nf=narrowSwap.Favicon(3);
        Require(narrowSwap.SwapsClose(3,3)&&Near((ns.left+ns.right)/2,(nf.left+nf.right)/2)&&ns.right<=narrowSwap.Title(3).left,"narrow selected tab swaps close onto the favicon without touching the title");
        Require(few.Closable(0,1)&&!few.SwapsClose(0,0),"wide tabs keep the trailing close on every tab");
        auto narrow=TabStrip::Layout(1180,12,3,0);Require(narrow.tab>=Size::tabIconOnly&&narrow.tab<Size::tabCloseAll&&narrow.Titled(),"12 tabs at 1180 are narrow but titled");
        Require(!narrow.HasClose()&&Near(narrow.Title(3).right,narrow.Surface(3).right-Space::md),"narrow titles take the close room");
        Require(few.HasClose()&&Near(few.Title(0).right,TabStrip::TitleSlot(few.Surface(0)).right),"wide tabs keep the reserved close slot");
        auto locked=TabStrip::Layout(1180,11,0,0,narrow.tab);Require(Near(locked.tab,narrow.tab),"locked width survives a pointer close");
        Require(Near(TabStrip::Layout(1180,13,0,0,narrow.tab).tab,TabStrip::TabWidth(1180,13)),"lock never widens past the fitting width");
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
    // Web viewport (Paper V3 Outline > Viewbox): 2 DIP outline left/right/bottom, flush
    // under the chrome (81 DIP, 121 with the bookmarks band), 8 DIP bottom corners
    // concentric with the 10 DIP window.
    {auto v=Viewport::Bounds(1180,815);
     Require(Near(v.left,2)&&Near(v.top,81)&&Near(v.right,1178)&&Near(v.bottom,813),"viewport is the Paper Viewbox {2,81,1178,813} at 1180x815, under the chrome");
     auto b=Viewport::Bounds(1180,815,true);Require(Near(b.top,121)&&Near(b.bottom,813),"with the bookmarks band the viewport starts at 121");
     Require(Near(Viewport::Radius(false),8)&&Near(Viewport::Radius(false)+Viewport::inset,Viewport::windowRadius),"restored viewport corner 8 DIP = window 10 - outline 2");
     Require(Near(Viewport::Radius(true),0),"maximised viewport corner is square like the window");
     auto tiny=Viewport::Bounds(3,50);Require(tiny.right>=tiny.left&&tiny.bottom>=tiny.top,"viewport never inverts when the client collapses");}
    std::puts("PASS: viewport 2 DIP outline under the chrome, 8/0 DIP corners; brand 16/.32 at back-centre X & favicon-centre Y, first tab at x44 (back + 6), nav pair 6|3|1|3 with forward at 45, Reload 83..115, field at 121, 6 DIP tab/toolbar gap, contiguous hit regions, overflow capacity, adaptive 128-32 DIP tabs with masked peeking overflow, equal close insets, 1 DIP white focus tokens (pointer 10%, keyboard 50%).");
    return 0;
}
