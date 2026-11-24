#pragma once
// Colour emoji in native DirectWrite text (user titles in the bookmarks bar, menus, drawer,
// tabs, profiles, toasts). Inter Display stays the font of ordinary text; emoji come from the
// system emoji font. The string is never changed: only font ranges of the layout are set.
// design/emoji-rendering-report.md has the diagnosis behind each rule.
//
// 1. Drawing needs D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT, otherwise the system fallback
//    (already "Segoe UI Emoji" for U+1F600, ZWJ, skin tones) is drawn as monochrome outlines.
// 2. Inter Display itself maps a few emoji-capable symbols (U+2600 sun, U+2764 heart, U+26A0,
//    U+2B1C, arrows...). DirectWrite fallback is per character coverage and ignores the
//    variation selector, so "\u2600\uFE0F" stays Inter's monochrome glyph. Presentation():
//    a cluster that asks for emoji (VS16, Emoji_Presentation=Yes, ZWJ / modifier / keycap /
//    tag sequence) is set to the system emoji font; one that asks for text (VS15) to the
//    system symbol font unless Inter already draws it. A bare text-default symbol keeps the
//    font that covers it (Unicode default: text presentation).
// 3. Colour glyph layers ignore the brush alpha (an emoji at opacity 0 stays fully visible):
//    a line that really draws colour glyphs gets its opacity from a layer instead.
#include <windows.h>
#include <d2d1.h>
#include <dwrite_2.h>
#include <string>
#include <vector>

namespace NativeChrome::EmojiText {
inline constexpr const wchar_t* emojiFamily = L"Segoe UI Emoji";     // i18n-exempt: DirectWrite family name, never shown
inline constexpr const wchar_t* symbolFamily = L"Segoe UI Symbol";   // i18n-exempt: DirectWrite family name, never shown

inline UINT32 CodePointAt(const std::wstring& s, size_t i, size_t* length) {
    const wchar_t c = s[i];
    if (IS_HIGH_SURROGATE(c) && i + 1 < s.size() && IS_LOW_SURROGATE(s[i + 1])) { *length = 2; return 0x10000u + ((UINT32(c) - 0xD800u) << 10) + (UINT32(s[i + 1]) - 0xDC00u); }
    *length = 1; return c;
}
// Emoji_Presentation=Yes below U+1F000 (Unicode emoji-data.txt): these default to emoji.
inline bool DefaultEmojiBmp(UINT32 c) {
    static constexpr UINT32 ranges[][2] = {{0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE}, {0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F3}, {0x26F5, 0x26F5}, {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797}, {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55}};
    for (auto& r : ranges) if (c >= r[0] && c <= r[1]) return true;
    return false;
}
// Characters that can start an emoji sequence: symbols and pictographs, keycap bases, (c) (r).
// Letters of any script never do, so a ZWJ inside Indic or Arabic text is left alone.
inline bool EmojiBase(UINT32 c) { return c >= 0x203C || c == '#' || c == '*' || (c >= '0' && c <= '9') || c == 0xA9 || c == 0xAE; }
inline bool Modifier(UINT32 c) { return c >= 0x1F3FB && c <= 0x1F3FF; }
inline bool Tag(UINT32 c) { return c >= 0xE0020 && c <= 0xE007F; }

struct Span { UINT32 start = 0, length = 0; bool emoji = false; UINT32 base = 0; };
// Clusters whose presentation must be set explicitly. Pure (no DirectWrite): tested alone.
inline std::vector<Span> Presentation(const std::wstring& s) {
    std::vector<Span> spans;
    for (size_t i = 0; i < s.size();) {
        size_t n = 0; const UINT32 base = CodePointAt(s, i, &n);
        size_t j = i + n; bool sequence = false, emojiSelector = false, textSelector = false;
        if (!EmojiBase(base) || (base >= 0xFE00 && base <= 0xFE0F) || base == 0x200D) { i = j; continue; }
        if (j < s.size() && s[j] == 0xFE0F) { emojiSelector = true; ++j; }
        else if (j < s.size() && s[j] == 0xFE0E) { textSelector = true; ++j; }
        // Extend over the rest of an emoji sequence so one font shapes it whole.
        while (!textSelector && j < s.size()) {
            size_t m = 0; const UINT32 next = CodePointAt(s, j, &m);
            if (next == 0xFE0F || next == 0x20E3 || Modifier(next) || Tag(next)) { j += m; sequence = true; continue; }
            if (next == 0x200D && j + 1 < s.size()) { size_t k = 0; CodePointAt(s, j + 1, &k); j += 1 + k; sequence = true; continue; }
            break;
        }
        if (textSelector) spans.push_back({UINT32(i), UINT32(j - i), false, base});
        else if (emojiSelector || sequence || DefaultEmojiBmp(base)) spans.push_back({UINT32(i), UINT32(j - i), true, base});
        i = j;
    }
    return spans;
}
// Cheap prefilter: false when no character of `s` can resolve to a colour font (plain Latin text,
// punctuation, typographic quotes...). Over-inclusive; Colored() gives the exact answer.
inline bool ColorCandidate(const std::wstring& s) {
    for (wchar_t c : s) if (IS_HIGH_SURROGATE(c) || (c >= 0x2100 && (c < 0xE000 || c > 0xF8FF))) return true;
    return false;
}

inline IDWriteFont* FirstFont(IDWriteFontCollection* collection, const wchar_t* family) {
    if (!collection) return nullptr;
    UINT32 index = 0; BOOL exists = FALSE; IDWriteFontFamily* f = nullptr; IDWriteFont* font = nullptr;
    if (FAILED(collection->FindFamilyName(family, &index, &exists)) || !exists || FAILED(collection->GetFontFamily(index, &f))) return nullptr;
    f->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font); f->Release(); return font;
}
inline bool Covers(IDWriteFont* font, UINT32 c) { BOOL has = FALSE; return font && SUCCEEDED(font->HasCharacter(c, &has)) && has; }

// Applies Presentation() to `layout` (text font: `textFamily` in `textFonts`). A family that is
// missing or lacks the base character leaves the cluster to the normal fallback: never tofu.
// Returns false when a VS15 cluster has no text font at all (only the emoji font draws it, e.g. a
// recent emoji with no Unicode text variant): the layout must then be drawn without colour.
inline bool Apply(IDWriteFactory* write, IDWriteTextLayout* layout, const std::wstring& s, IDWriteFontCollection* textFonts, const wchar_t* textFamily, bool* forced = nullptr) {
    if (forced) *forced = false;
    if (!write || !layout) return true;
    const auto spans = Presentation(s); if (spans.empty()) return true;
    IDWriteFontCollection* system = nullptr; if (FAILED(write->GetSystemFontCollection(&system, FALSE)) || !system) return true;
    IDWriteFont* emoji = FirstFont(system, emojiFamily); IDWriteFont* symbol = nullptr; IDWriteFont* text = FirstFont(textFonts, textFamily);
    bool changed = false, color = true;
    for (auto& sp : spans) {
        const DWRITE_TEXT_RANGE r{sp.start, sp.length};
        if (sp.emoji) { if (Covers(emoji, sp.base)) { layout->SetFontCollection(system, r); layout->SetFontFamilyName(emojiFamily, r); changed = true; } continue; }
        if (Covers(text, sp.base)) continue;   // VS15 on a symbol Inter draws: already text
        if (!symbol) symbol = FirstFont(system, symbolFamily);
        if (Covers(symbol, sp.base)) { layout->SetFontCollection(system, r); layout->SetFontFamilyName(symbolFamily, r); changed = true; }
        else color = false;
    }
    if (text) text->Release();
    if (symbol) symbol->Release();
    if (emoji) emoji->Release();
    system->Release();
    if (forced) *forced = changed;
    return color;
}
// Gives `layout` the line box of the text font alone (DirectWrite's default spacing: ascent + descent
// + line gap), so a label with an emoji keeps its neighbours' height and baseline. Layouts that
// already set their own spacing (PopupKit's CSS line boxes) are left as they are.
inline void KeepLineBox(IDWriteTextLayout* layout, IDWriteFontCollection* textFonts, const wchar_t* textFamily) {
    DWRITE_LINE_SPACING_METHOD method; FLOAT spacing = 0, current = 0;
    if (!layout || FAILED(layout->GetLineSpacing(&method, &spacing, &current)) || method != DWRITE_LINE_SPACING_METHOD_DEFAULT) return;
    IDWriteFont* text = FirstFont(textFonts, textFamily); if (!text) return;
    DWRITE_FONT_METRICS fm{}; text->GetMetrics(&fm); text->Release();
    FLOAT size = 0; layout->GetFontSize(0, &size); const float em = size / fm.designUnitsPerEm;
    layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, (fm.ascent + fm.descent + fm.lineGap) * em, (fm.ascent + fm.lineGap) * em);
}
// Exact: true when drawing `layout` uses a colour font face (fallback included). Runs the layout
// through a renderer that only reads the glyph runs' faces; call once per layout (cached).
inline bool Colored(IDWriteTextLayout* layout) {
    struct Probe : IDWriteTextRenderer {
        bool color = false;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override { if (id == __uuidof(IUnknown) || id == __uuidof(IDWritePixelSnapping) || id == __uuidof(IDWriteTextRenderer)) { *out = this; return S_OK; } *out = nullptr; return E_NOINTERFACE; }
        ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
        ULONG STDMETHODCALLTYPE Release() override { return 1; }
        HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* v) override { *v = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) override { *m = {1, 0, 0, 1, 0, 0}; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* v) override { *v = 1; return S_OK; }
        HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
            IDWriteFontFace2* face = nullptr;
            if (!color && run && run->fontFace && SUCCEEDED(run->fontFace->QueryInterface(__uuidof(IDWriteFontFace2), reinterpret_cast<void**>(&face)))) { color = face->IsColorFont(); face->Release(); }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }
    } probe;
    if (layout) layout->Draw(nullptr, &probe, 0, 0);
    return probe.color;
}
// Everything a drawer needs, decided once per layout.
struct Style { bool color = true;    // ENABLE_COLOR_FONT (false only for an uncovered VS15 cluster)
               bool layered = false; // colour glyphs present: opacity through a layer
};
inline Style Prepare(IDWriteFactory* write, IDWriteTextLayout* layout, const std::wstring& s, IDWriteFontCollection* textFonts, const wchar_t* textFamily) {
    Style st; if (!ColorCandidate(s)) return st;
    bool forced = false; st.color = Apply(write, layout, s, textFonts, textFamily, &forced);
    const bool colored = Colored(layout);
    st.layered = st.color && colored;
    if (forced || colored) KeepLineBox(layout, textFonts, textFamily);
    return st;
}
// Draws `layout` at `origin` in `color`. Text without colour glyphs takes the existing path (alpha
// in the brush). With colour glyphs, which ignore the brush alpha, the alpha is applied once to the
// whole line (text + emoji) through a layer; the brush is then opaque.
inline void Draw(ID2D1RenderTarget* t, D2D1_POINT_2F origin, IDWriteTextLayout* layout, ID2D1SolidColorBrush* brush, D2D1_COLOR_F color, Style st, D2D1_DRAW_TEXT_OPTIONS extra = D2D1_DRAW_TEXT_OPTIONS_NONE) {
    const auto options = D2D1_DRAW_TEXT_OPTIONS(extra | (st.color ? D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT : D2D1_DRAW_TEXT_OPTIONS_NONE));
    if (!st.layered || color.a >= 1.f) { brush->SetColor(color); t->DrawTextLayout(origin, layout, brush, options); return; }
    if (color.a <= 0.f) return;
    const float a = color.a; color.a = 1; brush->SetColor(color);
    t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), a), nullptr);
    t->DrawTextLayout(origin, layout, brush, options); t->PopLayer();
}
}
