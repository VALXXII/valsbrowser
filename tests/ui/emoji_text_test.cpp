// Colour emoji in native text (design/emoji-rendering-report.md). Offscreen Direct2D only, synthetic
// titles, no window, no WebView2, no profile. Uses only Chrome / PopupKit entry points that existed
// before the fix, so the same file runs against the old source (red) and the new one (green).
//  1. Chrome::Text (bar, menus, drawer, tabs): FE0F / Emoji_Presentation / ZWJ / modifiers / keycap
//     draw colour pixels; bare text-default symbols and FE0E stay monochrome; mixed on one line.
//  2. Opacity: an emoji at the band's rest alpha is attenuated, at 0 invisible; ordinary Inter text
//     is pixel-identical to the pre-fix drawing at 1, .64, .5, .32; a mixed line's text part too.
//  3. Measures: same line box / baseline with and without emoji, ZWJ sequences one cluster wide,
//     truncation stays inside its slot.
//  4. Production band (Chrome::Paint) at 96 / 144 / 192 DPI, rest and hover; tab titles; PopupKit
//     DrawText (profile menu, toasts). PNGs in <out> for review.
// Usage: emoji_text_test.exe <repo root> <output dir>
#include "../../src/tooltip.h"
#if __has_include("../../src/emoji-text.h")
#include "../../src/emoji-text.h"
#define HAVE_EMOJI_TEXT 1
#endif
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
using namespace NativeChrome;

static int failures = 0, checks = 0;
static void Expect(bool ok, const std::string& what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++failures; }
static std::string Fmt(const char* f, double a = 0, double b = 0, double c = 0) { char s[300]; std::snprintf(s, sizeof s, f, a, b, c); return s; }

static void Frame(Chrome& c, const std::function<void()>& draw) {
    auto* t = c.RenderTarget(); const float k = Design::Surface::canvas;
    t->BeginDraw(); t->Clear(D2D1::ColorF(k, k, k, 1)); draw(); t->EndDraw();
}
struct Stats { int colored = 0, ink = 0; long long sum = 0; };
// Pixels of a DIP rectangle: `colored` = chroma (max - min channel) above `chroma` (32), `ink` = brighter than the
// canvas by more than 8, `sum` = total brightness above the canvas.
static Stats Measure(Chrome& c, D2D1_RECT_F r, UINT dpi, int chroma = 32) {
    Stats s; const float k = dpi / 96.f; const int bg = 23;
    for (int y = (int)(r.top * k); y < (int)(r.bottom * k); ++y) for (int x = (int)(r.left * k); x < (int)(r.right * k); ++x) {
        const UINT32 p = c.SurfacePixel(x / k + .01f, y / k + .01f); const int b = p & 255, g = (p >> 8) & 255, rr = (p >> 16) & 255;
        const int mx = std::max({rr, g, b}), mn = std::min({rr, g, b});
        if (mx - mn > chroma) ++s.colored;
        if (mx > bg + 8) ++s.ink;
        s.sum += std::max(0, mx - bg);
    }
    return s;
}
static std::vector<UINT32> Pixels(Chrome& c, D2D1_RECT_F r, UINT dpi) {
    std::vector<UINT32> v; const float k = dpi / 96.f;
    for (int y = (int)(r.top * k); y < (int)(r.bottom * k); ++y) for (int x = (int)(r.left * k); x < (int)(r.right * k); ++x) v.push_back(c.SurfacePixel(x / k + .01f, y / k + .01f));
    return v;
}
static int MaxDiff(const std::vector<UINT32>& a, const std::vector<UINT32>& b) {
    if (a.size() != b.size()) return 999;
    int d = 0;
    for (size_t i = 0; i < a.size(); ++i) for (int s = 0; s < 24; s += 8) d = std::max(d, std::abs(int((a[i] >> s) & 255) - int((b[i] >> s) & 255)));
    return d;
}
// The pre-fix Chrome::Text, verbatim apart from the layout source: brush alpha, no colour option.
static void OldText(Chrome& c, const std::wstring& v, D2D1_RECT_F slot, float size, float opacity, bool medium, float tracking) {
    IDWriteTextLayout* layout = nullptr; TextMeasure m; if (FAILED(c.MakeTextLayout(v, slot, size, false, medium, tracking, &layout, m))) return;
    auto clip = slot; clip.top = std::min(slot.top, m.inkTop - 1); clip.bottom = std::max(slot.bottom, m.inkBottom + 1);
    auto* t = c.RenderTarget(); t->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED); c.SolidBrush()->SetColor(D2D1::ColorF(1, 1, 1, opacity));
    t->DrawTextLayout(D2D1::Point2F(slot.left, m.originY), layout, c.SolidBrush(), D2D1_DRAW_TEXT_OPTIONS_NONE); t->PopAxisAlignedClip(); layout->Release();
}
static const float labelSize = Design::Type::label.size, labelTracking = Design::Type::label.tracking; static const bool labelMedium = Design::Type::label.medium;
static void Label(Chrome& c, const std::wstring& s, D2D1_RECT_F slot, float opacity) { c.Text(s, slot, labelSize, opacity, false, labelMedium, labelTracking); }
static float Width(Chrome& c, const std::wstring& s) { return c.TextWidth(s, labelSize, labelMedium, false, labelTracking); }

struct Case { const char* name; std::wstring text; bool color; };
static std::vector<Case> Cases() {
    return {
        {"screenshot sun + VS16 (U+2600 U+FE0F, Inter covers U+2600)", L"Accueil \u2600\uFE0F \u00E9t\u00E9", true},
        {"screenshot sun bare (U+2600, text by default): stays Inter text", L"Accueil \u2600 \u00E9t\u00E9", false},
        {"sun + VS15: text", L"Accueil \u2600\uFE0E \u00E9t\u00E9", false},
        {"screenshot tools bare (U+1F6E0, no Inter glyph)", L"Travail \U0001F6E0", true},
        {"tools + VS16", L"Travail \U0001F6E0\uFE0F", true},
        {"tools + VS15: text (Segoe UI Symbol)", L"Travail \U0001F6E0\uFE0E", false},
        {"default emoji U+1F600", L"Perso \U0001F600", true},
        {"heart + VS16 (Inter covers U+2764)", L"Coup de c\u0153ur \u2764\uFE0F", true},
        {"Emoji_Presentation=Yes BMP U+2B50 star", L"Favoris \u2B50", true},
        {"ZWJ woman technologist", L"Dev \U0001F469\u200D\U0001F4BB", true},
        {"ZWJ with inner VS16 (couple with heart)", L"Nous \U0001F468\u200D\u2764\uFE0F\u200D\U0001F468", true},
        {"skin tone modifier", L"Ok \U0001F44D\U0001F3FD", true},
        {"keycap #FE0F 20E3", L"Num\u00E9ro #\uFE0F\u20E3", true},
        {"plain French text", L"Perso \u2014 l\u2019\u00E9t\u00E9 \u00AB\u00A0Accueil\u00A0\u00BB", false},
    };
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring root = argv[1], out = argv[2];
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#ifdef HAVE_EMOJI_TEXT
    {   // Pure presentation policy: UTF-16 indices of whole clusters; the string itself is const.
        using EmojiText::Presentation;
        const std::wstring s = L"a\u2600\uFE0E b\u2600\uFE0F c\U0001F6E0\uFE0E d\U0001F6E0\uFE0F e\U0001F469\u200D\U0001F4BB f\U0001F44D\U0001F3FD #\uFE0F\u20E3 \u2600 \u2B50 \u0915\u094D\u200D\u0937";
        const auto spans = Presentation(s); std::vector<std::wstring> got; std::vector<bool> emoji;
        for (auto& sp : spans) { got.push_back(s.substr(sp.start, sp.length)); emoji.push_back(sp.emoji); }
        const std::vector<std::wstring> want{L"\u2600\uFE0E", L"\u2600\uFE0F", L"\U0001F6E0\uFE0E", L"\U0001F6E0\uFE0F", L"\U0001F469\u200D\U0001F4BB", L"\U0001F44D\U0001F3FD", L"#\uFE0F\u20E3", L"\u2B50"};
        Expect(got == want && emoji == std::vector<bool>{false, true, false, true, true, true, true, true}, "presentation spans: FE0E text, FE0F / ZWJ / modifier / keycap / EP=Yes emoji, bare U+2600 and Devanagari ZWJ untouched, surrogates whole");
        Expect(Presentation(L"\U0001F468\u200D\u2764\uFE0F\u200D\U0001F468").size() == 1 && Presentation(L"\U0001F468\u200D\u2764\uFE0F\u200D\U0001F468")[0].length == 8, "ZWJ sequence with inner FE0F is one span of 8 UTF-16 units");
        Expect(Presentation(L"Perso").empty() && Presentation(L"").empty() && Presentation(L"\uFE0F").empty() && Presentation(L"\U0001F6E0").empty(), "plain text, lone selector and bare non-BMP emoji need no override");
    }
#else
    std::printf("note: src/emoji-text.h absent (pre-fix source): policy unit checks skipped\n");
#endif
    for (UINT dpi : {96u, 144u, 192u}) {
        const float area = (dpi / 96.f) * (dpi / 96.f);
        const std::string at = " @" + std::to_string(dpi) + " DPI";
        Chrome c; if (FAILED(c.InitOffscreen(root, dpi, 420, 40))) { Expect(false, "offscreen chrome" + at); continue; }
        const D2D1_RECT_F slot{8, 4, 412, 36}, all{0, 0, 420, 40};
        // 1. Colour per case, at full opacity (menu rows, hovered band items).
        for (auto& k : Cases()) {
            Frame(c, [&] { Label(c, k.text, slot, 1); });
            const auto s = Measure(c, all, dpi);
            if (dpi == 96) { std::wstring name = out + L"\\text-"; for (const char* p = k.name; *p && name.size() < out.size() + 40; ++p) name += (wchar_t)(isalnum((unsigned char)*p) ? *p : '-'); c.SavePng((name + L".png").c_str()); }
            Expect(k.color ? s.colored >= 6 * area : s.colored == 0, std::string(k.name) + Fmt(": %.0f colour px, %.0f ink px", s.colored, s.ink) + at);
            Expect(s.ink > 0, std::string(k.name) + ": drawn (no empty glyph)" + at);
        }
        {   // Regional-indicator flag: Windows' emoji font has no flag artwork (letters instead); never empty.
            Frame(c, [&] { Label(c, L"FR \U0001F1EB\U0001F1F7", slot, 1); }); const auto s = Measure(c, all, dpi);
            Expect(s.ink > 0, Fmt("flag U+1F1EB U+1F1F7: drawn (%.0f ink px, %.0f colour px: platform limit, letters)", s.ink, s.colored) + at);
            if (dpi == 96) c.SavePng((out + L"\\text-flag.png").c_str());
        }
        {   // Mixed selectors on one line: each cluster measured in its own column.
            const std::vector<std::pair<std::wstring, bool>> parts{{L"\u2600\uFE0E", false}, {L"\u2600\uFE0F", true}, {L"\U0001F6E0\uFE0E", false}, {L"\U0001F6E0\uFE0F", true}, {L"\u2764", false}, {L"\u2764\uFE0F", true}};
            std::wstring line; for (auto& p : parts) line += p.first + L"  ";
            Frame(c, [&] { Label(c, line, slot, 1); }); if (dpi == 96) c.SavePng((out + L"\\text-mixed-selectors.png").c_str());
            std::wstring prefix; bool each = true; std::string detail;
            for (auto& p : parts) {
                const float l = slot.left + Width(c, prefix), r = slot.left + Width(c, prefix + p.first);
                const auto s = Measure(c, {l, 0, r, 40}, dpi); if (p.second ? s.colored < 6 * area : s.colored != 0) each = false;
                detail += " " + std::to_string(s.colored); prefix += p.first + L"  ";
            }
            Expect(each, "one line: U+2600 FE0E text, U+2600 FE0F colour, U+1F6E0 FE0E text, U+1F6E0 FE0F colour, U+2764 text, U+2764 FE0F colour (colour px" + detail + ")" + at);
        }
        // 2. Opacity: the band at rest draws titles at 50 %, inactive tabs at 45 %, asleep 32 %.
        {
            const std::wstring e = L"\U0001F600";
            Frame(c, [&] { Label(c, e, slot, 1); }); const auto full = Measure(c, all, dpi);
            Frame(c, [&] { Label(c, e, slot, .32f); }); const auto dim = Measure(c, all, dpi);
            Frame(c, [&] { Label(c, e, slot, 0); }); const auto none = Measure(c, all, dpi);
            const double ratio = full.sum ? double(dim.sum) / full.sum : 1;
            Expect(ratio > .2 && ratio < .45, Fmt("emoji at 32 %% opacity is attenuated (brightness ratio %.2f of opaque)", ratio) + at);
            Expect(none.ink == 0, Fmt("emoji at 0 %% opacity is invisible (%.0f ink px)", none.ink) + at);
            Expect(dim.colored > 0, "attenuated emoji keeps its colour (not the monochrome brush)" + at);
        }
        for (float op : {1.f, .64f, .5f, .32f}) {
            const std::wstring plain = L"Perso \u2014 l\u2019\u00E9t\u00E9 2026 \u00AB\u00A0Wikip\u00E9dia\u00A0\u00BB";
            Frame(c, [&] { OldText(c, plain, slot, labelSize, op, labelMedium, labelTracking); }); const auto before = Pixels(c, all, dpi);
            Frame(c, [&] { Label(c, plain, slot, op); }); const auto after = Pixels(c, all, dpi);
            Expect(MaxDiff(before, after) == 0, Fmt("ordinary Inter text pixel-identical to the pre-fix drawing at %.2f opacity", op) + at);
        }
        {   // Mixed line at 50 %: the text part matches plain text at 50 % (alpha applied once).
            const std::wstring text = L"Perso ", mixed = text + L"\U0001F600";
            const float split = slot.left + Width(c, text) - 1;
            Frame(c, [&] { Label(c, text, slot, .5f); }); const auto alone = Pixels(c, {0, 0, split, 40}, dpi);
            Frame(c, [&] { Label(c, mixed, slot, .5f); }); const auto with = Pixels(c, {0, 0, split, 40}, dpi);
            const auto emoji = Measure(c, {split + 1, 0, 420, 40}, dpi);
            Frame(c, [&] { Label(c, mixed, slot, 1); }); const auto opaque = Measure(c, {split + 1, 0, 420, 40}, dpi);
            const int d = MaxDiff(alone, with); const double ratio = opaque.sum ? double(emoji.sum) / opaque.sum : 1;
            Expect(d <= 2 && ratio > .35 && ratio < .65, Fmt("mixed line at 50 %%: text within %.0f levels of plain text, emoji brightness %.2f of opaque", d, ratio) + at);
        }
        // 3. Measures.
        {
            const auto plain = c.MeasureText(L"Perso", slot, labelSize, labelMedium), emoji = c.MeasureText(L"Perso \U0001F600", slot, labelSize, labelMedium), sun = c.MeasureText(L"Accueil \u2600\uFE0F", slot, labelSize, labelMedium);
            Expect(std::fabs(plain.lineHeight - emoji.lineHeight) < .01f && std::fabs(plain.baseline - emoji.baseline) < .01f && std::fabs(plain.originY - emoji.originY) < .01f && std::fabs(plain.originY - sun.originY) < .01f,
                   Fmt("same line box with an emoji (height %.2f vs %.2f, origin %.2f) : band labels share one baseline", plain.lineHeight, emoji.lineHeight, emoji.originY) + at);
            const float one = Width(c, L"\U0001F600"), zwj = Width(c, L"\U0001F469\u200D\U0001F4BB"), couple = Width(c, L"\U0001F468\u200D\u2764\uFE0F\u200D\U0001F468"), skin = Width(c, L"\U0001F44D\U0001F3FD");
            Expect(std::fabs(zwj - one) < 1.5f && std::fabs(couple - one) < 1.5f && std::fabs(skin - one) < 1.5f, Fmt("ZWJ / inner FE0F / skin tone sequences shape as one glyph (%.1f, %.1f vs %.1f DIP)", zwj, couple, one) + at);
            // Truncation: a long title in a narrow slot keeps every pixel inside the slot.
            const std::wstring longTitle = L"\U0001F6E0\uFE0F Travail \u2600\uFE0F \u00E9t\u00E9 \U0001F469\u200D\U0001F4BB tr\u00E8s long titre de dossier";
            const D2D1_RECT_F narrow{8, 4, 120, 36};
            Frame(c, [&] { Label(c, longTitle, narrow, 1); }); const auto outside = Measure(c, {121, 0, 420, 40}, dpi), inside = Measure(c, {8, 0, 120, 40}, dpi);
            Expect(Width(c, longTitle) > 112 && outside.ink == 0 && inside.colored > 0, Fmt("truncated emoji title: %.0f DIP natural, nothing past the slot (%.0f px), colour inside", Width(c, longTitle), outside.ink) + at);
            if (dpi == 96) c.SavePng((out + L"\\text-truncated.png").c_str());
        }
        // 4a. PopupKit (profile menu rows, toasts): colour and opacity through the same rules.
        {
            const PopupKit::Type type{14, 20, 0, false, false};
            Frame(c, [&] { PopupKit::DrawText(c, L"Profil \u2600\uFE0F \U0001F6E0", {8, 10, 412, 30}, type, PopupKit::White(1)); }); const auto full = Measure(c, all, dpi);
            if (dpi == 96) c.SavePng((out + L"\\popup-text.png").c_str());
            Frame(c, [&] { PopupKit::DrawText(c, L"Profil \u2600\uFE0F \U0001F6E0", {8, 10, 412, 30}, type, PopupKit::White(.5f)); }); const auto half = Measure(c, all, dpi);
            Frame(c, [&] { PopupKit::DrawText(c, L"Profil \u2600 \U0001F6E0\uFE0E", {8, 10, 412, 30}, type, PopupKit::White(1)); }); const auto text = Measure(c, all, dpi);
            const double ratio = full.sum ? double(half.sum) / full.sum : 1;
            Expect(full.colored >= 12 * area && text.colored == 0 && ratio > .35 && ratio < .65, Fmt("PopupKit::DrawText: colour emoji (%.0f px), text presentation kept (%.0f px), 50 %% alpha applied (%.2f)", full.colored, text.colored, ratio) + at);
            Frame(c, [&] { PopupKit::DrawRich(c, L"youtube.com \U0001F600 est en plein \u00E9cran", 0, 11, {8, 10, 412, 30}, type, PopupKit::White(.5f), PopupKit::White(1)); }); const auto rich = Measure(c, {8, 0, 60, 40}, dpi);
            Frame(c, [&] { PopupKit::DrawRich(c, L"youtube.com est en plein \u00E9cran", 0, 11, {8, 10, 412, 30}, type, PopupKit::White(.5f), PopupKit::White(1)); }); const auto richPlain = Measure(c, {8, 0, 60, 40}, dpi);
            const double strong = richPlain.sum ? double(rich.sum) / richPlain.sum : 0;
            Expect(strong > .97 && strong < 1.03, Fmt("PopupKit::DrawRich: emphasised run keeps its own alpha next to an emoji (%.3f of plain)", strong) + at);
        }
    }
    // 4b. Production band and tabs (Chrome::Paint) at three densities, rest and hover.
    for (UINT dpi : {96u, 144u, 192u}) {
        const float area = (dpi / 96.f) * (dpi / 96.f); const std::string at = " @" + std::to_string(dpi) + " DPI";
        Chrome c; if (FAILED(c.InitOffscreen(root, dpi, 1180, 121))) { Expect(false, "offscreen band" + at); continue; }
        Motion::State m(false); c.SetMotion(&m); c.bandShown = true;
        auto item = [](unsigned id, const wchar_t* t, bool folder) { Chrome::BandItem b; b.id = id; b.title = t; b.tip = t; b.folder = folder; return b; };
        c.bandItems = {item(1, L"Accueil \u2600\uFE0F \u00E9t\u00E9", false), item(2, L"Travail \U0001F6E0\uFE0F", true), item(3, L"Accueil \u2600 \u00E9t\u00E9", false), item(4, L"Perso", true), item(5, L"Travail \U0001F6E0", false)};
        std::vector<std::wstring> titles{L"Accueil \u2600\uFE0F", L"Perso \U0001F600"};
        c.Paint(titles, 0, L"", false, 0, 0);
        c.SavePng((out + L"\\band-rest-" + std::to_wstring(dpi) + L".png").c_str());
        // Rest titles are drawn at 50 %: chroma threshold halved accordingly (the tools emoji is mostly grey).
        std::vector<Stats> rest; for (int i = 0; i < 5; ++i) rest.push_back(Measure(c, c.BandTitleBox(i), dpi, 12));
        Expect(rest[0].colored >= 6 * area && rest[1].colored >= 6 * area && rest[4].colored >= 6 * area, Fmt("band at rest: FE0F sun, folder tools, bare tools in colour (%.0f, %.0f, %.0f px)", rest[0].colored, rest[1].colored, rest[4].colored) + at);
        Expect(rest[2].colored == 0 && rest[3].colored == 0 && rest[2].ink > 0, "band at rest: bare sun stays Inter text, plain folder unchanged" + at);
        // The emoji follows the label's rest alpha (50 %): compared with the same label drawn opaque in place.
        const auto box = c.BandTitleBox(0); const float sunLeft = box.left + Width(c, L"Accueil "), sunRight = box.left + Width(c, L"Accueil \u2600\uFE0F");
        const D2D1_RECT_F sun{sunLeft, box.top, sunRight, box.bottom};
        const auto cold = Measure(c, sun, dpi);
        Frame(c, [&] { Label(c, c.bandItems[0].title, {box.left, box.top, box.left + Width(c, c.bandItems[0].title) + 2, box.bottom}, 1); }); const auto opaque = Measure(c, sun, dpi);
        const double ratio = opaque.sum ? double(cold.sum) / opaque.sum : 1;
        Expect(ratio > .4 && ratio < .6, Fmt("band emoji follows the label alpha at rest: %.2f of opaque (label 50 %%)", ratio) + at);
        // Hover (100 %) render for review.
        m.SetInteraction(BookmarkBase, 0, Motion::Now() - std::chrono::milliseconds(500));
        c.Paint(titles, 0, L"", false, BookmarkBase, 0, nullptr, Motion::Now() + std::chrono::milliseconds(400));
        c.SavePng((out + L"\\band-hover-" + std::to_wstring(dpi) + L".png").c_str());
        Expect(Measure(c, sun, dpi).colored >= 6 * area, "hovered band item: emoji in colour" + at);
        m.ClearInteractions(); c.Paint(titles, 0, L"", false, 0, 0);
        // Tabs: the selected and an inactive title, both through Chrome::Text.
        bool tabColour = false; for (auto& r : c.regions) if (IsTab(r.id)) { const auto s = Measure(c, r.rect, dpi); if (s.colored >= 6 * area) tabColour = true; }
        Expect(tabColour, "tab titles: emoji in colour" + at);
    }
    CoUninitialize();
    std::printf(failures ? "%d of %d emoji check(s) FAILED\n" : "all %d emoji checks passed\n", failures ? failures : checks, checks);
    return failures ? 1 : 0;
}
