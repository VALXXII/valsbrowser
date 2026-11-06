#pragma once
#include <algorithm>

namespace NativeChrome::Design {
// Values are DIPs; native DPI scaling happens only at the render boundary.
namespace Surface {
inline constexpr float canvas=23/255.f, raised=41.196f/255.f;
inline constexpr float selected=41/255.f, hover=51/255.f, pressed=62/255.f;
inline constexpr float tabHover=34/255.f, focus=190/255.f, separatorOpacity=77/255.f;
}
namespace Text {
inline constexpr float primary=1, secondary=.5f, muted=128/255.f*.9f, disabled=.32f, address=secondary;
}
namespace Space { inline constexpr float xs=4, sm=6, md=8, lg=16; }
namespace Radius { inline constexpr float control=6, inset=3; }
namespace Size {
inline constexpr float control=32, compact=24, icon=16, windowIcon=14;
inline constexpr float tab=128, tabPitch=140, closeSlot=24, focusStroke=2;
}
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
