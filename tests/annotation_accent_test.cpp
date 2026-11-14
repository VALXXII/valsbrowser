// Annotation mode × profile colour (design/annotation-profile-color-plan.md): pure checks, no Win32, no Direct2D.
//   - the Paper tones of the mode carried to every real profile fill (src/annotation-accent.h), the neutral « Personnel »
//     profile (fill = the reference #3E67FF, Paper P07) giving the Paper values back exactly;
//   - every text tone measured >= 4.5:1 on its composited backdrop, every hue / intensity (0 .. max);
//   - primary buttons: hover / press states (Design::Accent::StateColor) keep white >= 4.5:1; the old white veil did not;
//   - stored (Settled) vs presented (Get) accent;
//   - the cursor SVG and the rasters (wave, veil) take the accent; the defaults stay the Paper pixels.
// Build: g++ -std=c++17 -Wall -Wextra -Werror -pedantic -static -Isrc tests/annotation_accent_test.cpp
#include "../src/annotation-accent.h"
#include "../src/annotation-dither.h"
#include "../src/annotation-script.h"
#include "../src/design-system.h"
#include <cstdio>
#include <string>
#include <vector>

namespace T = NativeChrome::Annotations::AccentTone;
namespace D = NativeChrome::Annotations::Dither;
namespace A = NativeChrome::Design::Accent;
namespace PC = ProfileColor;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static std::uint32_t Fill(int hue, float chroma) { return T::Pack(PC::AccentFill(hue, chroma)); }
static double HueGap(double a, double b) { double d = std::fabs(a - b); return d > 180 ? 360 - d : d; }
static std::uint32_t WhiteOver(std::uint32_t c, double a) { return T::Over(0xFFFFFF, a, c); }
static std::uint32_t Rgb24(NativeChrome::Design::Rgb c) { return (std::uint32_t(std::lround(c.r * 255)) << 16) | (std::uint32_t(std::lround(c.g * 255)) << 8) | std::uint32_t(std::lround(c.b * 255)); }

int main() {
    // ---- 1. Personnel (neutral, P07): the reference blue; the exact Paper tones wherever they meet their threshold.
    {   const T::Band b = T::BandFor(T::reference);
        Check(b.gradientEnd == 0x233FD6 && b.label == 0x5B84FF && b.border == 0x3E67FF3D, "reference fill: gradient #233FD6, label #5B84FF (4.5:1 met on every row), border #3E67FF3D (Paper)");
        Check(b.markLight == 0xD1DFFF && b.markMid == 0x3296FF && b.markDeep == 0x2F63E7 && b.waveDim == D::Wave::dim && b.waveBright == D::Wave::bright, "reference fill: mark ramp and wave dots are the Paper values");
        Check(T::Relative(T::reference, 0x5B84FF) == 0x5B84FF, "Relative(reference, tone) == tone, exactly");
        // Paper's « BETA » #6A8FFF fails on the chip's lower rows (review of e204fd6): it is lifted, hue kept, minimally.
        const double paperBetaBottom = T::Contrast(0x6A8FFF, T::BetaBackdrop(T::reference, 0x233FD6));
        Check(paperBetaBottom < 4.5 && b.beta != 0x6A8FFF, "Paper BETA #6A8FFF is below 4.5:1 on the chip bottom row, so Personnel's BETA is lifted (not kept)");
        const T::Lch was = T::ToOklch(0x6A8FFF), now = T::ToOklch(b.beta);
        Check(HueGap(was.h, now.h) < 2 && now.L > was.L && now.L - was.L < .03, "Personnel BETA: same hue, lightness raised by < 0.03 (minimal)");
        std::printf("     Personnel: « Mode Annotation » #%06X >= %.2f:1 on every row; « BETA » #%06X >= %.2f:1 on every chip row (Paper #6A8FFF: %.2f:1 at the bottom)\n",
                    unsigned(b.label), T::WorstContrast(b.label, T::LabelBackdrops(b.gradientEnd)), unsigned(b.beta), T::WorstContrast(b.beta, T::BetaBackdrops(b.fill, b.gradientEnd)), paperBetaBottom);
    }
    // ---- 1b. The review's reproducer: hue 288, intensity .10 => fill #756DAF. e204fd6 chose BETA #948EC1, floored on the
    //          chip's MIDDLE row only: 4.32:1 measured natively at device row 38 (y 19 DIP). Every row must pass now.
    {   const std::uint32_t fill = Fill(288, .10f); const T::Band b = T::BandFor(fill);
        Check(fill == 0x756DAF, "reproducer fill hue 288 / .10 is #756DAF");
        const std::uint32_t old = T::Readable(T::Relative(fill, 0x6A8FFF), T::Over(fill, .16, T::BandAt(b.gradientEnd, 16)), 4.5);
        const double oldAt19 = T::Contrast(old, T::Over(fill, .16, T::BandAt(b.gradientEnd, 19)));
        std::printf("     reproducer: e204fd6 BETA #%06X = %.2f:1 at y 19 (review measured 4.32 natively); now #%06X >= %.2f:1 on rows 7..25\n",
                    unsigned(old), oldAt19, unsigned(b.beta), T::WorstContrast(b.beta, T::BetaBackdrops(fill, b.gradientEnd)));
        Check(old == 0x948EC1 && oldAt19 < 4.5, "regression evidence: the middle-row floor gives #948EC1, below 4.5:1 lower in the chip");
        Check(T::WorstContrast(b.beta, T::BetaBackdrops(fill, b.gradientEnd)) >= 4.5, "reproducer: BETA >= 4.5:1 on every chip row (7..25 DIP)");
        Check(T::WorstContrast(b.label, T::LabelBackdrops(b.gradientEnd)) >= 4.5, "reproducer: label >= 4.5:1 on every label row (6..26 DIP)");
    }
    // ---- 2. Every hue x intensity of a real profile fill: identity kept, text >= 4.5:1, roles ordered.
    {   double worstLabel = 99, worstBeta = 99, worstHue = 0; int worstLabelHue = -1, worstBetaHue = -1; bool ordered = true, fillHueKept = true, roundTrip = true;
        std::vector<std::uint32_t> all{T::reference};
        for (int hue = 0; hue < 360; ++hue) for (float c : {.0f, .02f, .04f, .06f, .08f, .10f, .12f, .14f, .16f}) all.push_back(Fill(hue, c));
        for (std::uint32_t fill : all) {
            const int hue = int(T::ToOklch(fill).h); const T::Band b = T::BandFor(fill);
            const double label = T::WorstContrast(b.label, T::LabelBackdrops(b.gradientEnd)), beta = T::WorstContrast(b.beta, T::BetaBackdrops(fill, b.gradientEnd));
            if (label < worstLabel) { worstLabel = label; worstLabelHue = hue; }
            if (beta < worstBeta) { worstBeta = beta; worstBetaHue = hue; }
            const T::Lch f = T::ToOklch(fill), g = T::ToOklch(b.gradientEnd), l = T::ToOklch(b.label), w = T::ToOklch(b.waveBright >> 8);
            if (!(g.L < f.L && l.L > f.L && w.L > f.L)) ordered = false;   // gradient deeper, label and bright dots lighter
            if (f.C > .04) {   // each tone keeps the fill's hue plus the offset its Paper value has from #3E67FF
                const std::uint32_t tones[5] = {b.gradientEnd, b.label, b.beta, b.markMid, b.markDeep}, paper[5] = {0x233FD6, 0x5B84FF, 0x6A8FFF, 0x3296FF, 0x2F63E7};
                for (int i = 0; i < 5; ++i) { const T::Lch t = T::ToOklch(tones[i]); if (t.C > .03) worstHue = std::max(worstHue, HueGap(t.h, f.h + T::ToOklch(paper[i]).h - T::ToOklch(T::reference).h)); }
            }
            if ((b.border >> 8) != fill || (b.waveDim >> 8) != fill) fillHueKept = false;
            const std::uint32_t back = T::Relative(fill, T::reference);   // the fill's own tone is the fill (within a byte)
            if (std::abs(int(back >> 16) - int(fill >> 16)) > 1 || std::abs(int((back >> 8) & 255) - int((fill >> 8) & 255)) > 1 || std::abs(int(back & 255) - int(fill & 255)) > 1) roundTrip = false;
        }
        std::printf("     all fills: « Mode Annotation » >= %.2f:1 (hue %d), « BETA » >= %.2f:1 (hue %d), tones within %.1f deg of (fill hue + Paper offset)\n", worstLabel, worstLabelHue, worstBeta, worstBetaHue, worstHue);
        Check(worstLabel >= 4.5, "« Mode Annotation » >= 4.5:1 on EVERY row of its box, every profile fill (1.4.3 AA)");
        Check(worstBeta >= 4.5, "« BETA » >= 4.5:1 on EVERY row of its chip, every profile fill (1.4.3 AA)");
        Check(worstHue <= 5, "every tone keeps the fill hue + its Paper offset (<= 5 deg: byte rounding at low chroma; offsets <= 14 deg)");
        Check(ordered, "roles kept: gradient end deeper than the fill, label and bright dots lighter");
        Check(fillHueKept, "border and dim dots are the fill itself (alpha only)");
        Check(roundTrip, "Relative(fill, reference) == fill within 1 level");
    }
    // ---- 3. Readable: a passing tone is never touched; a failing one is lifted (hue kept) until it passes.
    {   Check(T::Readable(0x5B84FF, 0x1A2140, 4.5) == 0x5B84FF, "Readable leaves a passing pair unchanged");
        const std::uint32_t dark = 0x403080, up = T::Readable(dark, 0x171717, 4.5);
        Check(T::Contrast(up, 0x171717) >= 4.5 && HueGap(T::ToOklch(up).h, T::ToOklch(dark).h) < 3 && T::ToOklch(up).L > T::ToOklch(dark).L, "Readable lifts L only (hue kept) to reach 4.5:1");
        const std::vector<std::uint32_t> rows{0x171717, 0x202020, 0x303030};
        const std::uint32_t all = T::ReadableOnAll(dark, rows, 4.5);
        Check(T::WorstContrast(all, rows) >= 4.5 && T::Contrast(T::Readable(dark, 0x171717, 4.5), 0x303030) < 4.5, "ReadableOnAll floors on the lightest row, where a single-row floor would not");
        Check(T::BandAt(0x233FD6, 25) != T::BandAt(0x233FD6, 16) && T::Contrast(0xFFFFFF, T::BandAt(0x233FD6, 25)) < T::Contrast(0xFFFFFF, T::BandAt(0x233FD6, 7)), "the band lightens downwards (the bottom row is the lightest)");
    }
    // ---- 4. Primary buttons « Ajouter », « Terminé », « Annoter cette page »: white on every state >= 4.5:1.
    {   double worstNew = 99, worstOld = 99; std::uint32_t oldAt = 0;
        std::vector<std::uint32_t> fills{T::reference};
        for (int hue = 0; hue < 360; hue += 5) for (float c : {.0f, .06f, .14f, .16f}) fills.push_back(Fill(hue, c));
        for (std::uint32_t f : fills) {
            const NativeChrome::Design::Rgb fill{((f >> 16) & 255) / 255.f, ((f >> 8) & 255) / 255.f, (f & 255) / 255.f};
            for (float hover : {0.f, .5f, 1.f}) for (float press : {0.f, .5f, 1.f}) {
                worstNew = std::min(worstNew, T::Contrast(0xFFFFFF, Rgb24(A::StateColor(fill, hover, press))));
                const double old = T::Contrast(0xFFFFFF, WhiteOver(WhiteOver(f, .08 * hover), .06 * press));   // before: white 8 % / 6 % veils
                if (old < worstOld) { worstOld = old; oldAt = f; }
            }
        }
        std::printf("     primary buttons: white >= %.2f:1 in every state (before: %.2f:1, fill #%06X hovered + pressed)\n", worstNew, worstOld, unsigned(oldAt));
        Check(worstNew >= 4.5, "primary buttons: white >= 4.5:1 at rest, hover and press, every profile fill");
        Check(worstOld < 4.5, "regression evidence: the former white hover veil fell below 4.5:1");
        std::printf("     Personnel « Ajouter »: rest %.2f, hover %.2f, pressed %.2f (before: hover %.2f)\n",
                    T::Contrast(0xFFFFFF, T::reference), T::Contrast(0xFFFFFF, Rgb24(A::StateColor(A::Get(), 1, 0))), T::Contrast(0xFFFFFF, Rgb24(A::StateColor(A::Get(), 1, 1))), T::Contrast(0xFFFFFF, WhiteOver(T::reference, .08)));
    }
    // ---- 5. Settled (stored) vs Get (on screen during the 120 ms fade).
    {   A::Set(0x3E67FF); Check(Rgb24(A::Get()) == 0x3E67FF && Rgb24(A::Settled()) == 0x3E67FF, "Set: presented and settled are the same");
        A::SettledCurrent() = {0x1A / 255.f, 0x86 / 255.f, 0x41 / 255.f}; A::Current() = {.2f, .4f, .6f};   // a fade step towards green
        Check(Rgb24(A::Settled()) == 0x1A8641 && Rgb24(A::Get()) != 0x1A8641, "during a fade: Settled is the stored colour, Get the step on screen");
        A::Set(0x3E67FF);
    }
    // ---- 6. Cursor dot, rasters.
    {   const std::string blue = D::Cursor::Svg(), green = D::Cursor::Svg(0x1A8641);
        Check(blue.find("fill='#3E67FF'") != std::string::npos && green.find("fill='#1A8641'") != std::string::npos && green.find("#3E67FF") == std::string::npos, "cursor « viseur »: the dot is the accent given (#3E67FF by default)");
        std::vector<std::uint32_t> a(size_t(300) * 200), b(a.size()), g(a.size());
        D::RasterVeil({a.data(), 300, 300, 200}, 1, .12, 1, true); D::RasterVeil({b.data(), 300, 300, 200}, 1, .12, 1, true, 0x3E67FF);
        Check(a == b, "veil: the default is the reference accent (Paper pixels unchanged)");
        D::RasterVeil({g.data(), 300, 300, 200}, 1, .12, 1, true, 0x1A8641);
        bool greenOnly = true; int lit = 0;
        for (std::uint32_t p : g) if (p >> 24) { ++lit; const int r = (p >> 16) & 255, gg = (p >> 8) & 255, bb = p & 255; if (!(gg >= r && gg >= bb)) greenOnly = false; }
        Check(lit > 1000 && greenOnly, "veil in a green profile: every lit pixel is green-dominant (no blue left)");
        D::Zones z; z.left = 100; z.right = 1000; const T::Band gb = T::BandFor(0x1A8641);
        std::vector<std::uint32_t> w(size_t(1180) * 31); const int dots = D::RasterBand({w.data(), 1180, 1180, 31}, 1, .7, 1, z, gb.waveDim, gb.waveBright);
        bool waveGreen = true; for (std::uint32_t p : w) if (p >> 24) { const int r = (p >> 16) & 255, gg = (p >> 8) & 255, bb = p & 255; if (!(gg >= r && gg >= bb)) waveGreen = false; }
        Check(dots > 0 && waveGreen, "band wave in a green profile: green dots only");
    }
    // ---- 7. Page script: the accent rules can be replaced live, the gap pink is not an accent rule.
    {   const std::wstring js = NativeChrome::Annotations::PageScript();
        Check(js.find(L"op === 'accent'") != std::wstring::npos && js.find(L"accentStyle.textContent = accentCss()") != std::wstring::npos, "page script: an « accent » command rebuilds the accent rules");
        const size_t css = js.find(L"function accentCss()"), end = js.find(L"root.innerHTML", css);
        const std::wstring rules = js.substr(css, end - css);
        Check(rules.find(L".f{") != std::wstring::npos && rules.find(L".h,.v{") != std::wstring::npos && rules.find(L".l{") != std::wstring::npos && rules.find(L".pb{") != std::wstring::npos && rules.find(L".pl{") != std::wstring::npos, "accent rules: frame fill, edges, label, padding hatches, padding labels");
        Check(rules.find(L"224,71,158") == std::wstring::npos && rules.find(L"D63E92") == std::wstring::npos, "the gap pink is not in the accent rules (semantic colour kept)");
        Check(js.find(L"rgbList(arg && arg.accent)") != std::wstring::npos, "the « accent » argument is validated (r,g,b ints) before use");
    }
    std::printf("annotation_accent_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
