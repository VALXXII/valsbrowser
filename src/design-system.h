#pragma once
#include <algorithm>
#include <cmath>

namespace NativeChrome::Design {
// Values are DIPs; native DPI scaling happens only at the render boundary.
namespace Surface {
inline constexpr float canvas=23/255.f, raised=41.196f/255.f;
inline constexpr float selected=41/255.f, hover=51/255.f, pressed=62/255.f;
inline constexpr float tabHover=34/255.f, separatorOpacity=77/255.f;
}
namespace Text {
inline constexpr float primary=1, secondary=.5f, muted=128/255.f*.9f, disabled=.32f, address=secondary;
}
namespace Space {
inline constexpr float xs=4, sm=6, md=8, lg=16;
// One inter-control gap for the whole chrome: back->forward is 38->44 (6 DIP),
// so visible tab->tab and last tab->new-tab use the same 6 DIP.
inline constexpr float controlGap=sm, tabGap=controlGap;
}
namespace Radius { inline constexpr float control=6, inset=3; }
namespace Size {
inline constexpr float control=32, compact=24, icon=16, windowIcon=14;
// tabPitch = visible tab + gap. Was 140 (12 DIP visible gap, double the toolbar).
// `tab` is the natural (widest) tab; crowded strips shrink tabs down to `tabMin`,
// a favicon-only square the height of every other control.
inline constexpr float tab=128, tabPitch=tab+Space::tabGap, closeSlot=24;
inline constexpr float tabMin=control;
// Below `tabCloseAll` tabs drop the close slot so the title keeps the room (middle
// click, context menu and Ctrl+W still close); below `tabIconOnly` a tab is just
// its centred favicon.
inline constexpr float tabCloseAll=96, tabIconOnly=56;
// Close slot is inset equally from the tab's trailing, top and bottom edges
// (4 DIP), so the centered 16 DIP glyph box sits 8 DIP from each edge,
// mirroring the favicon's 8 DIP leading inset.
inline constexpr float closeInset=(control-closeSlot)/2;
// Focus ring: exactly 1 DIP white. Pointer focus keeps the user-chosen subtle
// 10% alpha; keyboard focus (Tab, arrows, shortcuts) rises to 50% so it meets
// WCAG 1.4.11 (>= 3:1) on every chrome surface it crosses (issue #3).
inline constexpr float focusStroke=1;
}
namespace AddressLoading { inline constexpr float opacity=.08f; }
namespace Focus {
inline constexpr float color=1, pointerOpacity=.10f, keyboardOpacity=.50f, stroke=Size::focusStroke;
inline constexpr float Opacity(bool keyboard){return keyboard?keyboardOpacity:pointerOpacity;}
}
static_assert(Size::tabPitch-Size::tab==Space::tabGap,"visible tab gap equals toolbar gap");
static_assert(Size::closeInset*2+Size::closeSlot==Size::control,"close slot vertically centred in the 32 DIP tab");
static_assert(Size::focusStroke==1,"focus stroke is the user-chosen 1 DIP");

// Pure tab-strip geometry (DIPs). Renderers and hit testing call these so
// visible gaps, hit areas and overflow capacity can never drift apart.
struct Box { float left, top, right, bottom; };
// Toolbar column edges (DIP) that the tab row aligns to: back {6,38}, forward {44,76}.
namespace Toolbar {
inline constexpr float backLeft=Space::sm, backCenterX=backLeft+Size::control/2;
inline constexpr float forwardLeft=backLeft+Size::control+Space::controlGap;
}
namespace TabStrip {
// Region origin chosen so the first visible tab surface starts at forwardLeft (44):
// left 38 + tabGap 6 = 44. The brand region owns 0..38.
inline constexpr float left=Toolbar::forwardLeft-Space::tabGap;
inline constexpr float regionTop=2, regionBottom=40, top=6, bottom=38;
inline constexpr float trailingReserve=118; // window controls, unchanged
// Hit region: contiguous, gap belongs to the tab it precedes (no dead zone between tabs).
inline constexpr Box Region(int slot){return {left+Size::tabPitch*slot,regionTop,left+Size::tabPitch*(slot+1),regionBottom};}
// Visible surface: 6 DIP leading gap inside the region, then 128 DIP.
inline constexpr Box Surface(int slot){return {left+Size::tabPitch*slot+Space::tabGap,top,left+Size::tabPitch*slot+Space::tabGap+Size::tab,bottom};}
inline constexpr Box CloseSlot(Box tab){return {tab.right-Size::closeInset-Size::closeSlot,tab.top+Size::closeInset,tab.right-Size::closeInset,tab.bottom-Size::closeInset};}
inline constexpr Box FaviconSlot(Box tab){return {tab.left+Space::md,tab.top+Space::md,tab.left+Space::md+Size::icon,tab.top+Space::md+Size::icon};}
// Title never changes width with hover/focus: it always reserves the close slot.
inline constexpr Box TitleSlot(Box tab){return {tab.left+32,tab.top,CloseSlot(tab).left-Space::xs,tab.bottom};}
// New-tab button keeps the same 6 DIP visible gap after the last tab.
inline constexpr float NewTabLeft(int shown){return left+Size::tabPitch*shown+Space::tabGap;}
inline int Capacity(float width){const float room=width-trailingReserve-left-Space::tabGap-Size::control;return std::max(1,static_cast<int>(room/Size::tabPitch));}
// Edge mask width: hidden tabs keep a peeking sliver under a canvas-coloured
// linear gradient on the side where they are, so overflow is never silent.
inline constexpr float fade=24, peek=Size::tabMin/2;
inline constexpr Box IconSlot(Box tab){const float x=(tab.left+tab.right-Size::icon)/2;return {x,tab.top+Space::md,x+Size::icon,tab.top+Space::md+Size::icon};}
// Region width available to tab pitches before the new-tab button.
inline float Room(float width){return std::max(Size::tabMin+Space::tabGap,width-trailingReserve-left-Space::tabGap-Size::control);}
// Adaptive width: tabs share the room evenly between 128 and 32 DIP, whole DIPs.
inline float TabWidth(float width,int count){if(count<=0)return Size::tab;return std::clamp(std::floor(Room(width)/count-Space::tabGap),Size::tabMin,Size::tab);}
// A laid-out, horizontally scrolled strip. Same invariants as the slot helpers
// above (6 DIP gap, contiguous regions, first tab at x44 when unscrolled), with
// `tab` adaptive and `scroll` shifting content under a [ViewLeft, ViewRight] clip.
struct Strip {
    int count=0; float tab=Size::tab, room=0, view=0, scroll=0;
    float Pitch()const{return tab+Space::tabGap;}
    float Content()const{return count*Pitch();}
    float MaxScroll()const{return std::max(0.f,Content()-view);}
    bool Overflow()const{return Content()>room;}
    float ViewLeft()const{return left+Space::tabGap;}
    float ViewRight()const{return left+view;}
    bool HiddenBefore()const{return scroll>.5f;}
    bool HiddenAfter()const{return scroll<MaxScroll()-.5f;}
    float ClearLeft()const{return ViewLeft()+(HiddenBefore()?fade:0);}
    float ClearRight()const{return ViewRight()-(HiddenAfter()?fade:0);}
    Box Region(int i)const{return {left+Pitch()*i-scroll,regionTop,left+Pitch()*(i+1)-scroll,regionBottom};}
    Box Surface(int i)const{const float x=left+Pitch()*i-scroll+Space::tabGap;return {x,top,x+tab,bottom};}
    // Fully outside the fades: drawn unmasked, keyboard-focusable, closable.
    bool Clear(int i)const{auto s=Surface(i);return s.left>=ClearLeft()-.01f&&s.right<=ClearRight()+.01f;}
    bool Visible(int i)const{auto s=Surface(i);return s.right>ViewLeft()&&s.left<ViewRight();}
    bool Titled()const{return tab>=Size::tabIconOnly;}
    bool HasClose()const{return tab>=Size::tabCloseAll;}
    // Title reserves the close slot whenever the tab can show one (no hover shift).
    // `...In` take a surface so a dragged (offset) tab lays out the same way.
    Box TitleIn(Box s)const{auto t=TitleSlot(s);if(!HasClose())t.right=s.right-Space::md;return t;}
    Box FaviconIn(Box s)const{return Titled()?FaviconSlot(s):IconSlot(s);}
    Box Title(int i)const{return TitleIn(Surface(i));}
    // Tabs too narrow for a trailing close slot swap their favicon for the close glyph
    // on hover/focus, centred on the favicon. Selected tab only: on an inactive tab
    // that spot must keep selecting it, never close it by surprise.
    bool SwapsClose(int i,int selected)const{return !HasClose()&&i==selected;}
    bool Closable(int i,int selected)const{return HasClose()||SwapsClose(i,selected);}
    Box CloseIn(Box s)const{if(HasClose())return CloseSlot(s);auto f=FaviconIn(s);const float x=(f.left+f.right)/2,y=(f.top+f.bottom)/2,h=Size::closeSlot/2;return {x-h,y-h,x+h,y+h};}
    Box Favicon(int i)const{return FaviconIn(Surface(i));}
    float NewTabLeft()const{return ViewRight()+Space::tabGap;}
};
// `locked` (0 = off) caps the width while the pointer stays on the strip after a
// pointer close, so the next close button lands under the cursor.
inline Strip Layout(float width,int count,int selected,float scroll,float locked=0){
    Strip s;s.count=std::max(0,count);s.room=std::floor(Room(width));s.tab=TabWidth(width,s.count);if(locked>0)s.tab=std::min(s.tab,std::max(locked,Size::tabMin));
    // Overflowing view = whole pitches + the first hidden tab's `peek`, so at either
    // scroll end the hidden neighbour shows exactly half a favicon-only tab.
    s.view=s.Overflow()?std::floor((s.room-Space::tabGap-peek)/s.Pitch())*s.Pitch()+Space::tabGap+peek:s.Content();
    float x=std::clamp(scroll,0.f,s.MaxScroll());
    if(selected>=0&&selected<s.count&&s.Overflow()){
        // Keep the selected tab clear of both fades; the neighbour peeks under one.
        const float start=s.Pitch()*selected,end=start+Space::tabGap+s.tab;
        x=std::min(x,start-fade);x=std::max(x,end-s.view+fade);
    }
    s.scroll=std::clamp(std::round(x),0.f,s.MaxScroll());return s;
}
}
static_assert(Size::tabMin==2*Space::md+Size::icon,"favicon-only tab keeps the 8 DIP favicon inset on both sides");
static_assert(Size::tabIconOnly<Size::tabCloseAll&&Size::tabCloseAll<=Size::tab,"tab compaction thresholds are ordered");
static_assert(TabStrip::left==38&&TabStrip::Surface(0).left==Toolbar::forwardLeft,"first tab surface aligns with the forward button (44)");
// Brand flower: optically placed on the toolbar column and tab row, not the
// geometric centre of its 0..38 region (which would be x 19).
namespace Brand {
inline constexpr float size=Size::icon, opacity=.32f;
inline constexpr float centerX=Toolbar::backCenterX;                                  // 22: back button centre
inline constexpr float centerY=TabStrip::top+Space::md+Size::icon/2;                  // 22: tab favicon centre
inline constexpr Box Region(){return {0,TabStrip::regionTop,TabStrip::left,TabStrip::bottom};}   // hit/focus 0..38
inline constexpr Box Glyph(){return {centerX-size/2,centerY-size/2,centerX+size/2,centerY+size/2};} // {14,14,30,30}
}
static_assert(Brand::centerX==22&&Brand::centerY==22,"brand flower centred at 22,22");
static_assert(Brand::centerY*2==TabStrip::FaviconSlot(TabStrip::Surface(0)).top+TabStrip::FaviconSlot(TabStrip::Surface(0)).bottom,"brand shares the favicon centre line");
inline constexpr unsigned long NativeGray(float value){auto byte=static_cast<unsigned long>(value*255+.5f);return byte|(byte<<8)|(byte<<16);}
struct TypeRole { float size; bool medium; float tracking; };
namespace Type {
inline constexpr TypeRole label{14,true,-.28f}, address{16,false,-.16f};
}
enum class ButtonVariant { Ghost, Raised, Embedded, Window, TabClose };
struct ButtonState { float hover=0, press=0; bool disabled=false, focus=false; };
struct ButtonStyle { float surface, radius, iconSize, iconOpacity; bool fill, focus; };
inline ButtonStyle Resolve(ButtonVariant variant,ButtonState state={}) {
    const float h=state.disabled?0:std::clamp(state.hover,0.f,1.f);
    const float p=state.disabled?0:std::clamp(state.press,0.f,1.f);
    const bool raised=variant==ButtonVariant::Raised||variant==ButtonVariant::Embedded;
    const float base=raised?Surface::raised:Surface::canvas;
    // Interpolate opaque colors from the actual parent: never transparent black.
    const float hovered=base+(Surface::hover-base)*h;
    return {hovered+(Surface::pressed-hovered)*p,
        variant==ButtonVariant::Embedded?Radius::inset:Radius::control,
        variant==ButtonVariant::Window?Size::windowIcon:Size::icon,
        state.disabled?Text::disabled:Text::primary,
        variant!=ButtonVariant::TabClose&&(raised||h>0||p>0),state.focus&&!state.disabled};
}
inline float TabSurface(bool selected,float hover) {
    return selected?Surface::selected:Surface::canvas+(Surface::tabHover-Surface::canvas)*std::clamp(hover,0.f,1.f);
}
}
