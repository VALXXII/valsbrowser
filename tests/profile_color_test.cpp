// Profile colours (src/profile-color.h): every hue and intensity must stay legible (Paper P06/P07).
#include "../src/profile-color.h"
#include <cstdio>
#include <string>

namespace PC = ProfileColor;
static int failures = 0;
static void Check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

int main() {
    const PC::Rgb8 white{255, 255, 255};
    double minFill = 99, maxFill = 0, minInk = 99, minPlate = 99; int badHue = -1;
    for (int hue = 0; hue < 360; ++hue)
        for (float c : {.02f, .06f, .10f, .14f, .16f}) {
            const double fill = PC::Contrast(white, PC::AccentFill(hue, c));
            const PC::Rgb8 ink = PC::Ink(hue, c);
            const double onCanvas = PC::Contrast(ink, PC::Canvas), onPlate = PC::Contrast(ink, PC::Over(ink, .10));
            if (fill < minFill) { minFill = fill; badHue = hue; }
            if (fill > maxFill) maxFill = fill;
            if (onCanvas < minInk) minInk = onCanvas;
            if (onPlate < minPlate) minPlate = onPlate;
        }
    std::printf("     white on fill %.2f..%.2f (worst hue %d), ink on canvas >= %.2f, ink on 10%% plate >= %.2f\n", minFill, maxFill, badHue, minInk, minPlate);
    Check(minFill >= 4.6, "white text on every accent fill (all hues, all intensities) >= 4.6:1");
    Check(maxFill < 6.0, "fills are the lightest that pass (not needlessly dark)");
    Check(minInk >= 8.0, "ink (OKLCH L 0.78) on the canvas >= 8:1 for every hue");
    Check(minPlate >= 6.9, "ink on its own 10 % plate >= 6.9:1 for every hue (AA needs 4.5)");
    Check(PC::Contrast(white, PC::NeutralFill()) >= 4.6 && PC::Neutral() == PC::Rgb8{0xB3, 0xB3, 0xB3} && PC::Ink(120, 0) == PC::Neutral() && PC::AccentFill(40, 0) == PC::NeutralFill(), "intensity 0 is the neutral grey (#B3B3B3 ink, #747474 fill)");
    Check(PC::Ink(-30, .14f) == PC::Ink(330, .14f) && PC::Ink(390, .14f) == PC::Ink(30, .14f), "hue wraps (-30 = 330, 390 = 30)");
    Check(PC::Ink(200, 5.f) == PC::Ink(200, PC::MaxChroma), "intensity clamped to the maximum");
    Check(PC::Hex({0x1A, 0x86, 0x41}) == L"#1A8641", "hex formatting");

    int hue = -1; float chroma = -1;
    Check(PC::FromHex(L"#1A2A6C", hue, chroma) && hue >= 255 && hue <= 275 && chroma > .05f, "dark hex: hue and chroma kept (Paper « Hex sombre », 265°)");
    Check(PC::FromHex(L"  b3b3b3 ", hue, chroma) && chroma == 0, "grey hex gives the neutral colour");
    Check(PC::FromHex(L"#abc", hue, chroma), "short #RGB accepted");
    int keepHue = 7; float keepChroma = .1f;
    Check(!PC::FromHex(L"#12345", keepHue, keepChroma) && !PC::FromHex(L"#GG0000", keepHue, keepChroma) && !PC::FromHex(L"", keepHue, keepChroma) && keepHue == 7 && keepChroma == .1f, "invalid hex refused, outputs untouched");
    bool roundTrip = true;
    for (int h = 0; h < 360; h += 15) { int back = 0; float bc = 0; PC::FromHex(PC::Hex(PC::Ink(h, .12f)), back, bc); int d = std::abs(back - h) % 360; d = std::min(d, 360 - d); if (d > 3) roundTrip = false; }
    Check(roundTrip, "hex of an ink colour reads back to the same hue (±3°)");
    Check(std::wstring(PC::HueName(200)) == L"cyan" && std::wstring(PC::HueName(210)) == L"azur" && std::wstring(PC::HueName(240)) == L"bleu" && std::wstring(PC::HueName(-1)) == L"rose" && std::wstring(PC::HueName(0)) == L"rouge", "French hue names in 30° buckets");
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall profile colour checks passed\n", failures);
    return failures ? 1 : 0;
}
