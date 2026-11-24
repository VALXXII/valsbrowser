#pragma once
// The address field's « Local » pill (#127, Paper « Browser Design » › « #127 — Panneau latéral » › « Proposition — Adresse ·
// pastille « Local » (validée) »): shown instead of « Non sécurisé » on a project of this computer (AddressBar::Security::
// Local). Its colours follow the PROFILE colour, like the profile button (Val, 2026-11-24):
//   icon + text  the profile's ink (ProfileColor::Ink, the profile button's own colour; a private window's eye-slash ink)
//   plate        the ink at 10 % (16 % on hover), as a SOLID colour over the surface the pill sits on:
//                c = surface + (ink − surface) × a (ProfileColor::Over, the profile plates' blend)
//   divider      after the pill: #545454 in a #292929 field, #454545 without a field (unchanged)
// Two surfaces: the top bar's field (#292929; also #127's column panel) and #127's compact bar (no field, on #171717).
// Geometry (Design::Address::local*): 20 high, radius 4, padding 5, Phosphor « monitor » 14, « Local » 13/16, gap 4.
// Pure C++17, no Win32: unit-tested in tests/address_test.cpp.
#include "profile-color.h"

namespace LocalPill {

inline constexpr ProfileColor::Rgb8 Field{0x29, 0x29, 0x29}, Bare{0x17, 0x17, 0x17};
inline constexpr ProfileColor::Rgb8 FieldDivider{0x54, 0x54, 0x54}, BareDivider{0x45, 0x45, 0x45};
inline constexpr double Plate = .10, PlateHover = .16;

struct Colors { ProfileColor::Rgb8 plate, hover, ink, divider; };
// `ink`: the profile's ink; `inField`: in a #292929 field (top bar, column) or on the bare #171717 (compact bar).
inline Colors For(ProfileColor::Rgb8 ink, bool inField) {
    const auto surface = inField ? Field : Bare;
    return {ProfileColor::Over(ink, Plate, surface), ProfileColor::Over(ink, PlateHover, surface), ink, inField ? FieldDivider : BareDivider};
}

} // namespace LocalPill
