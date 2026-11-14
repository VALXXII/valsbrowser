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
// One inter-control gap for the whole chrome (6 DIP): forward->field, brand->first tab,
// tab->tab and last tab->new-tab. Back/Forward are one joined pair (see Toolbar).
inline constexpr float controlGap=sm, tabGap=controlGap;
}
// `button` is the one corner for every rectangular button, taken from the toolbar
// Downloads button (Ghost, 6 DIP): Copy link, Reload, tab close, popup remove,
// settings icon buttons and update-toast actions all share it. `inset` stays for
// non-button focus perimeters (text links).
namespace Radius { inline constexpr float control=6, inset=3, button=control;
// Profiles scale (Paper p-I-0 tokens --radius-browser-4/8/12/16): key caps, rows/plates, menus, dialogs.
inline constexpr float r4=4, r8=8, r12=12, r16=16; }
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
// Danger tone (Paper --color-browser-danger #FF7B6E): an invalid certificate,
// "Effacer les données…". Always paired with an icon and a label, never alone.
namespace Danger { inline constexpr float r=1, g=.482f, b=.431f, plate=.14f; }
// Accent (Paper --color-browser-accent #3E67FF): the text caret of every native field,
// later the user-chosen highlight. One accessor so a future "Couleur d'accentuation"
// setting only calls SetAccent (the value persists in `settings` as accent=#RRGGBB).
struct Rgb { float r, g, b; };
namespace Accent {
inline constexpr unsigned long defaultRgb=0x3E67FF;
inline constexpr float caretOpacity=.9f;   // Paper #3E67FFE6 / #FF7B6EE6
inline Rgb& Current(){static Rgb value{0x3E/255.f,0x67/255.f,0xFF/255.f};return value;}
inline void Set(unsigned long rgb){Current()={((rgb>>16)&255)/255.f,((rgb>>8)&255)/255.f,(rgb&255)/255.f};}
inline Rgb Get(){return Current();}
// Ink: the accent as a foreground on the dark chrome (progress, download ring and ripple, checks).
// The reference blue by default; a coloured profile sets its OKLCH L .78 ink (Paper p-I-0 P07).
inline Rgb& InkCurrent(){static Rgb value{0x3E/255.f,0x67/255.f,0xFF/255.f};return value;}
inline void SetInk(unsigned long rgb){InkCurrent()={((rgb>>16)&255)/255.f,((rgb>>8)&255)/255.f,(rgb&255)/255.f};}
inline Rgb Ink(){return InkCurrent();}
// Filled accent controls under white text (primary buttons, « Créer le profil »): rest / hover / pressed are
// ALL derived from the fill on screen (the window's profile fill, or a draft's), never from fixed colours: a
// coloured profile keeps its hue under the pointer (user report 2026-11-14: hover went back to the old blue via
// the legacy #5779FF / #3557D6 pair). Hover and press DEEPEN the fill (mixed toward black, hue kept): profile
// fills are solved at the lightest L where white text is >= 4.6:1, so any lightening would break the 4.5:1
// minimum (the legacy #5779FF itself is ~3.7:1). Each state is then clamped to white >= 4.5:1, and every
// interpolation between rest, hover and pressed stays on the same darkening line (contrast never below the
// lower of its ends). Read per paint: a profile switch or colour edit repaints the new hue at once.
inline constexpr float minWhiteContrast=4.5f, hoverDeepen=.08f, pressedDeepen=.16f;
struct States{Rgb rest,hover,pressed;};
inline float Linear(float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}
inline float Luminance(Rgb c){return .2126f*Linear(c.r)+.7152f*Linear(c.g)+.0722f*Linear(c.b);}
inline float WhiteContrast(Rgb c){return 1.05f/(Luminance(c)+.05f);}
inline Rgb Deepen(Rgb c,float k){k=std::clamp(k,0.f,1.f);return {c.r*(1-k),c.g*(1-k),c.b*(1-k)};}
inline Rgb Mix(Rgb a,Rgb b,float t){t=std::clamp(t,0.f,1.f);return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
inline Rgb Reach(Rgb fill,float k){Rgb c=Deepen(fill,k);while(WhiteContrast(c)<minWhiteContrast&&k<.9f){k+=.01f;c=Deepen(fill,k);}return c;}
inline States StatesOf(Rgb fill){return {fill,Reach(fill,hoverDeepen),Reach(fill,pressedDeepen)};}
// The colour painted for hover / press progress (0..1, animated): rest -> hover, then -> pressed.
inline Rgb StateColor(Rgb fill,float hover,float press){const auto s=StatesOf(fill);return Mix(Mix(s.rest,s.hover,hover),s.pressed,press);}
}
inline Rgb DangerRgb(){return {Danger::r,Danger::g,Danger::b};}
// Vertical chrome (Paper "Favoris — flow", 2026-11-08): the page starts right under the
// chrome, with no top padding (4 DIP on the sides and bottom only). Chrome = tab row
// 2+36, gap 6, toolbar 32, bottom padding 4, 1 DIP hairline = 81; the optional
// bookmarks band adds its 6 DIP gap and 34 DIP row (32 DIP items, bottom-aligned) = 121.
namespace Vertical {
inline constexpr float toolbarBottom=76, hairline=80, top=81;
inline constexpr float bandGap=Space::sm, bandRow=34, bandTop=toolbarBottom+bandGap+(bandRow-32), bandBottom=bandTop+32; // 84..116
inline constexpr float bandExtra=bandGap+bandRow; // 40
inline constexpr float ContentTop(bool band){return top+(band?bandExtra:0);}
}
static_assert(Vertical::ContentTop(false)==81&&Vertical::ContentTop(true)==121,"Paper chrome heights: 81 without the bookmarks band, 121 with it");
// Address bar (issue #20). The indicator lives in a 24 DIP slot 4 DIP inside the
// field, the same lane as the suggestion icons; text starts at 32 DIP.
namespace Address {
inline constexpr float slotInset=Space::xs, slot=Size::compact, textInset=slotInset+slot+Space::xs; // 4, 24, 32
inline constexpr float chipRadius=4, chipIcon=14, chipPadLeft=Space::xs, chipPadRight=Space::md, chipPlate=.06f;
inline constexpr float divider=Space::md, fade=24, primary=Text::primary, secondary=Text::secondary;
}
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
// Web viewport, the native frame that hosts the WebView2 site (Paper "Lisière —
// prototype actuel V3": Outline > Viewbox). The site sits flush under the chrome
// (square top corners; Vertical::ContentTop: 81 DIP, 121 with the bookmarks band) inside
// a 2 DIP canvas outline on its left, right and bottom edges, with 8 DIP bottom corners
// concentric with Paper's 10 DIP window corner (8+2). Maximised, the window corner is
// square, so the viewport corner is too. (Ported from the composition viewport onto the
// windowed pages: the window itself keeps DWM's native rounding, see RoundNatively.)
namespace Viewport {
inline constexpr float top=Vertical::top, inset=2, radius=8, windowRadius=radius+inset;
inline constexpr Box Bounds(float width,float height,bool band=false){return {inset,Vertical::ContentTop(band),std::max(inset,width-inset),std::max(Vertical::ContentTop(band),height-inset)};}
inline constexpr float Radius(bool maximized){return maximized?0:radius;}
}
static_assert(Viewport::windowRadius==10,"viewport corner stays concentric with the 10 DIP window corner");
// Toolbar navigation pair (Paper, Page 1 "Chrome", 2026-11-09 "Bouton avant arrière
// retouché"): Back and Forward are one joined segment. Each half is 32 x 32 on the raised
// surface with 6 DIP outer corners and 3 DIP inner corners; a 1 x 10 hairline (white at
// --opacity-browser-focus 10 %) sits between them with 3 DIP on each side:
// back {6,38}, hairline {41,42}, forward {45,77}. Then, 6 DIP apart, a standalone Reload
// (32 x 32, #FFFFFF14 = the raised surface, radius 6) at {83,115}, and the field at 121.
// It is the only Reload/Stop: the field's embedded Reload was removed (user request after
// review), so the field holds Copy and the star only.
namespace Toolbar {
inline constexpr float backLeft=Space::sm, backCenterX=backLeft+Size::control/2, backRight=backLeft+Size::control;
inline constexpr float pairGap=3, innerRadius=Radius::inset, separatorWidth=1, separatorHeight=10, separatorOpacity=.10f;
inline constexpr float separatorLeft=backRight+pairGap;
inline constexpr float forwardLeft=separatorLeft+separatorWidth+pairGap, forwardRight=forwardLeft+Size::control;
inline constexpr float reloadLeft=forwardRight+Space::controlGap, reloadRight=reloadLeft+Size::control;
inline constexpr float addressLeft=reloadRight+Space::controlGap;
}
static_assert(Toolbar::forwardLeft==45&&Toolbar::reloadLeft==83&&Toolbar::addressLeft==121,"Paper: back 6..38, hairline 41, forward 45..77, reload 83..115, field at 121");
namespace TabStrip {
// Region origin chosen so the first visible tab surface starts one control gap after
// the back button (44): left 38 + tabGap 6 = 44. The brand region owns 0..38.
inline constexpr float left=Toolbar::backRight;
inline constexpr float regionTop=2, regionBottom=40, top=6, bottom=38;
inline constexpr float trailingReserve=118; // window controls, unchanged
// Width reserved before the first tab (private window: the « Privée » chip + the 6 DIP row gap);
// 0 in a normal window, which keeps every position below unchanged.
inline float leading=0;
inline float Left(){return left+leading;}
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
inline int Capacity(float width){const float room=width-trailingReserve-Left()-Space::tabGap-Size::control;return std::max(1,static_cast<int>(room/Size::tabPitch));}
// Edge mask width: hidden tabs keep a peeking sliver under a canvas-coloured
// linear gradient on the side where they are, so overflow is never silent.
inline constexpr float fade=24, peek=Size::tabMin/2;
inline constexpr Box IconSlot(Box tab){const float x=(tab.left+tab.right-Size::icon)/2;return {x,tab.top+Space::md,x+Size::icon,tab.top+Space::md+Size::icon};}
// Region width available to tab pitches before the new-tab button.
inline float Room(float width){return std::max(Size::tabMin+Space::tabGap,width-trailingReserve-Left()-Space::tabGap-Size::control);}
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
    float ViewLeft()const{return Left()+Space::tabGap;}
    float ViewRight()const{return Left()+view;}
    bool HiddenBefore()const{return scroll>.5f;}
    bool HiddenAfter()const{return scroll<MaxScroll()-.5f;}
    float ClearLeft()const{return ViewLeft()+(HiddenBefore()?fade:0);}
    float ClearRight()const{return ViewRight()-(HiddenAfter()?fade:0);}
    Box Region(int i)const{return {Left()+Pitch()*i-scroll,regionTop,Left()+Pitch()*(i+1)-scroll,regionBottom};}
    Box Surface(int i)const{const float x=Left()+Pitch()*i-scroll+Space::tabGap;return {x,top,x+tab,bottom};}
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
static_assert(TabStrip::left==38&&TabStrip::Surface(0).left==Toolbar::backRight+Space::tabGap,"first tab surface sits one control gap after the back button (44)");
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
// Profile button (Paper p-I-0 P01 YSV/YSW, user edits 2026-11): it takes the flower's exact slot in a
// normal window: plate 32 at (6,6) radius 8, Phosphor Fill glyph 16 centred on (22,22) in the profile
// ink. Plate = ink at 10 % rest, 16 % hover (120 ms), 22 % pressed or menu open (+ scale .96);
// keyboard focus keeps 10 % and adds the 1 DIP white 50 % ring. Private windows keep the flower.
namespace ProfileButton {
inline constexpr float size=Size::control, left=6, top=TabStrip::top, radius=Radius::r8, glyph=Size::icon;
inline constexpr float rest=.10f, hover=.16f, pressed=.22f, pressScale=.96f;
inline constexpr Box Plate(){return {left,top,left+size,top+size};}
}
static_assert(ProfileButton::Plate().left+ProfileButton::size/2==Brand::centerX&&ProfileButton::Plate().top+ProfileButton::size/2==Brand::centerY,"profile glyph centred where the flower was");
static_assert(ProfileButton::Plate().right==TabStrip::left,"plate ends where the tab region starts (first tab at 44)");
// Private window chip (Paper LRZ-0 / LZZ-0, concept G84-0): after the brand, before the tabs, h 24
// bottom-aligned 4 above the tab row bottom, padding 6 / 8, gap 6, radius 6, split tint 16 %,
// eye-slash bold 16 white, « Privée » Inter Display Medium 14/18 white. The label width is measured
// (DirectWrite), never a constant; the chip is not interactive.
namespace PrivateChip {
inline constexpr float height=24, padLeft=6, padRight=8, gap=6, radius=6, glyph=Size::icon, bottom=TabStrip::bottom-4, top=bottom-height;
inline constexpr float left=TabStrip::left+Space::tabGap;   // 44, the first tab's place in a normal window
inline constexpr float tintR=62/255.f, tintG=103/255.f, tintB=1, tintA=.16f;
inline constexpr const wchar_t* label=L"Privée";
inline constexpr const char* eyeSlash="M56.88,31.93A12,12,0,1,0,39.12,48.07l16,17.65C20.67,88.66,5.72,121.58,5,123.13a12.08,12.08,0,0,0,0,9.75c.37.82,9.13,20.26,28.49,39.61C59.37,198.34,92,212,128,212a131.34,131.34,0,0,0,51-10l20.09,22.1a12,12,0,0,0,17.76-16.14ZM128,188c-29.59,0-55.47-10.73-76.91-31.88A130.69,130.69,0,0,1,29.52,128c5.27-9.31,18.79-29.9,42.84-44.29l90,99A109.33,109.33,0,0,1,128,188Zm123-55.12c-.36.81-9,20-28,39.16a12,12,0,1,1-17-16.9A130.48,130.48,0,0,0,226.48,128a130.36,130.36,0,0,0-21.57-28.12C183.46,78.73,157.59,68,128,68c-3.35,0-6.7.14-10,.42a12,12,0,1,1-2-23.91c3.93-.34,8-.51,12-.51,36,0,68.63,13.67,94.49,39.52,19.35,19.35,28.11,38.8,28.48,39.61A12.08,12.08,0,0,1,251,132.88Z";
inline float Width(float labelWidth){return padLeft+glyph+gap+std::ceil(labelWidth)+padRight;}
inline Box Rect(float labelWidth){return {left,top,left+Width(labelWidth),bottom};}
}
static_assert(PrivateChip::top==10&&PrivateChip::left==44,"Paper LRZ-0: chip at x 44, y 10");
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
        Radius::button,
        variant==ButtonVariant::Window?Size::windowIcon:Size::icon,
        state.disabled?Text::disabled:Text::primary,
        variant!=ButtonVariant::TabClose&&(raised||h>0||p>0),state.focus&&!state.disabled};
}
inline float TabSurface(bool selected,float hover) {
    return selected?Surface::selected:Surface::canvas+(Surface::tabHover-Surface::canvas)*std::clamp(hover,0.f,1.f);
}
}
