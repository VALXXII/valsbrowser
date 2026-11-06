#pragma once
#include "motion.h"

namespace NativeChrome { namespace Motion {
// Geometry is visual only; center on the existing glyph/control rectangle.
// Disabled controls and controls opting out of transforms never scale.
inline float PressScale(float press, bool reduced = false, bool enabled = true,
                        bool isStatic = false) noexcept {
    return reduced || !enabled || isStatic ? 1.f : 1.f-.04f*Clamp(press);
}
struct IconLayer { float opacity = 0, scale = 1; };
struct CopyLayers { IconLayer copy, check; };
// Render these in the same slot with Phosphor Bold assets. No blur render target,
// backing surface, geometry allocation or redraw outside the chrome is needed.
inline CopyLayers CopyPresentation(SuccessVisual success, bool reduced = false) noexcept {
    const float a = Clamp(success.opacity);
    return {{1-a, reduced ? 1.f : 1.f-.75f*a},
            {a, reduced ? 1.f : .25f+.75f*a}};
}
}} // namespace NativeChrome::Motion
