#pragma once
#include <algorithm>

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
inline constexpr float tab=128, tabPitch=tab+Space::tabGap, closeSlot=24;
// Close slot is inset equally from the tab's trailing, top and bottom edges
// (4 DIP), so the centered 16 DIP glyph box sits 8 DIP from each edge,
// mirroring the favicon's 8 DIP leading inset.
inline constexpr float closeInset=(control-closeSlot)/2;
// User-specified keyboard focus: exactly 1 DIP, white at 10% alpha.
inline constexpr float focusStroke=1;
}
namespace AddressLoading { inline constexpr float opacity=.08f; }
namespace Focus { inline constexpr float color=1, opacity=.10f, stroke=Size::focusStroke; }
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
}
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
inline constexpr TypeRole label{14,true,-.28f}, address{16,false,-.16f}, brand{8,false,-.16f};
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
