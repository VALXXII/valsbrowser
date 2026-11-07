// Native update toast renderer + owned layered popup. See update-toast.h and
// build/coordination/update-toast-contract.md. No WebView2, no browser main.
#include "update-toast.h"
#include "update-toast-motion.h"
#include "frame-clock.h"
#include "motion-presentation.h"
#include "design-system.h"
#include <d2d1.h>
#include <dwrite_1.h>
#include <dwmapi.h>
#include <cstdlib>
#include <map>

namespace BrowserUpdate { namespace ToastDetail {
using namespace ToastModel;
namespace Motion = NativeChrome::Motion;
template <class T> inline void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Phosphor Bold path data, verbatim from the Paper boards (MIT, assets/icons/phosphor-bold/LICENSE).
struct IconPath { const char* d; float view; };
inline IconPath IconData(Glyph g) {
    switch (g) {
    case Glyph::Info: return {"M108,84a16,16,0,1,1,16,16A16,16,0,0,1,108,84Zm128,44A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Zm-72,36.68V132a20,20,0,0,0-20-20,12,12,0,0,0-4,23.32V168a20,20,0,0,0,20,20,12,12,0,0,0,4-23.32Z", 256};
    case Glyph::Download: return {"M228,144v64a12,12,0,0,1-12,12H40a12,12,0,0,1-12-12V144a12,12,0,0,1,24,0v52H204V144a12,12,0,0,1,24,0Zm-108.49,8.49a12,12,0,0,0,17,0l40-40a12,12,0,0,0-17-17L140,115V32a12,12,0,0,0-24,0v83L96.49,95.51a12,12,0,0,0-17,17Z", 256};
    case Glyph::CheckCircle: return {"M176.49,95.51a12,12,0,0,1,0,17l-56,56a12,12,0,0,1-17,0l-24-24a12,12,0,1,1,17-17L112,143l47.51-47.52A12,12,0,0,1,176.49,95.51ZM236,128A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Z", 256};
    case Glyph::ArrowDown: return {"M91.215 66.714l-31.5 31.5a5.25 5.25 0 0 1-7.437 0l-31.5-31.5a5.25 5.25 0 0 1 7.437-7.437L50.751 81.812V17.5a5.25 5.25 0 0 1 10.5 0V81.812l22.536-22.54a5.25 5.25 0 0 1 7.437 7.438Z", 112};
    case Glyph::Timer: return {"M56.001 19.25a42 42 0 1 0 42 42A42.048 42.048 90 0 0 56.001 19.25Zm0 73.5a31.5 31.5 0 1 1 31.5-31.5A31.535 31.535 0 0 1 56.001 92.75ZM71.965 43.535a5.25 5.25 0 0 1 0 7.438l-12.25 12.25a5.25 5.25 0 0 1-7.437-7.438l12.25-12.25A5.25 5.25 0 0 1 71.965 43.535ZM40.251 7A5.25 5.25 0 0 1 45.501 1.75h21a5.25 5.25 0 0 1 0 10.5H45.501A5.25 5.25 0 0 1 40.251 7Z", 112};
    }
    return {"", 1};
}

// Minimal SVG path parser (M L H V C A Z, absolute and relative, implicit repeats).
inline HRESULT BuildPath(ID2D1Factory* factory, const char* d, ID2D1PathGeometry** out) {
    *out = nullptr;
    ID2D1PathGeometry* path = nullptr; ID2D1GeometrySink* sink = nullptr;
    HRESULT hr = factory->CreatePathGeometry(&path);
    if (SUCCEEDED(hr)) hr = path->Open(&sink);
    if (FAILED(hr)) { Drop(path); return hr; }
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    const char* p = d; char cmd = 0; bool open = false, ok = true;
    D2D1_POINT_2F cur{0, 0}, start{0, 0};
    auto skip = [&] { while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') ++p; };
    auto num = [&](float& v) { skip(); char* end = nullptr; v = std::strtof(p, &end); if (end == p) return false; p = end; return true; };
    auto begin = [&](D2D1_POINT_2F at) { if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN); sink->BeginFigure(at, D2D1_FIGURE_BEGIN_FILLED); open = true; start = at; };
    auto ensure = [&] { if (!open) begin(cur); };
    while (ok) {
        skip();
        if (!*p) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) cmd = *p++;
        else if (!cmd) { ok = false; break; }
        const bool rel = cmd >= 'a';
        const D2D1_POINT_2F o = rel ? cur : D2D1::Point2F(0, 0);
        switch (cmd | 0x20) {
        case 'm': { float x, y; if (!num(x) || !num(y)) { ok = false; break; } cur = {o.x + x, o.y + y}; begin(cur); cmd = rel ? 'l' : 'L'; break; }
        case 'l': { float x, y; if (!num(x) || !num(y)) { ok = false; break; } ensure(); cur = {o.x + x, o.y + y}; sink->AddLine(cur); break; }
        case 'h': { float x; if (!num(x)) { ok = false; break; } ensure(); cur.x = o.x + x; sink->AddLine(cur); break; }
        case 'v': { float y; if (!num(y)) { ok = false; break; } ensure(); cur.y = (rel ? cur.y : 0) + y; sink->AddLine(cur); break; }
        case 'c': { float v[6]; for (float& f : v) if (!num(f)) { ok = false; break; } if (!ok) break; ensure();
            D2D1_BEZIER_SEGMENT b{{o.x + v[0], o.y + v[1]}, {o.x + v[2], o.y + v[3]}, {o.x + v[4], o.y + v[5]}};
            sink->AddBezier(b); cur = b.point3; break; }
        case 'a': { float v[7]; for (float& f : v) if (!num(f)) { ok = false; break; } if (!ok) break; ensure();
            const D2D1_POINT_2F end{o.x + v[5], o.y + v[6]};
            sink->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(v[0], v[1]), v[2],
                v[4] != 0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                v[3] != 0 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            cur = end; break; }
        case 'z': if (open) { sink->EndFigure(D2D1_FIGURE_END_CLOSED); open = false; } cur = start; break;
        default: ok = false; break;
        }
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    hr = sink->Close(); sink->Release();
    if (FAILED(hr) || !ok) { Drop(path); return FAILED(hr) ? hr : E_INVALIDARG; }
    *out = path; return S_OK;
}

// Private font collection over the shipped Inter Display files (same files as chrome).
const wchar_t* const FontFiles[] = {L"InterDisplay-Regular.ttf", L"InterDisplay-Medium.ttf"};
struct FontEnumerator final : IDWriteFontFileEnumerator {
    LONG refs = 1; IDWriteFactory* factory; std::wstring folder; int index = -1; IDWriteFontFile* file = nullptr;
    FontEnumerator(IDWriteFactory* f, std::wstring p) : factory(f), folder(std::move(p)) { factory->AddRef(); }
    ~FontEnumerator() { Drop(file); Drop(factory); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontFileEnumerator)) { *out = this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* more) override {
        Drop(file); *more = ++index < (int)(sizeof(FontFiles) / sizeof(FontFiles[0]));
        if (!*more) return S_OK;
        return factory->CreateFontFileReference((folder + FontFiles[index]).c_str(), nullptr, &file);
    }
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** out) override { if (!file) return E_FAIL; *out = file; file->AddRef(); return S_OK; }
};
struct FontLoader final : IDWriteFontCollectionLoader {
    LONG refs = 1; std::wstring folder; explicit FontLoader(std::wstring p) : folder(std::move(p)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontCollectionLoader)) { *out = this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* f, const void*, UINT32, IDWriteFontFileEnumerator** out) override {
        *out = new FontEnumerator(f, folder); return S_OK;
    }
};

enum class Role { Title, Body, Button, Stat };
struct RoleSpec { bool medium; float size, line, tracking; bool features, wrap, center; };
inline RoleSpec Spec(Role r) {
    switch (r) {
    case Role::Title: return {true, Paper::titleSize, Paper::titleLine, Paper::titleTracking, true, false, false};
    case Role::Body: return {false, Paper::bodySize, Paper::bodyLine, Paper::bodyTracking, true, true, false};
    case Role::Button: return {true, Paper::buttonSize, 14, Paper::buttonTracking, false, false, true};
    case Role::Stat: return {false, Paper::statSize, 14, Paper::statTracking, true, false, false};
    }
    return {};
}

// Button visuals for the current layer (outgoing layers draw at rest).
struct ButtonVisual { float hover = 0, press = 0; bool focus = false, keyboard = false; };

class Painter {
public:
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* write = nullptr;
    FontLoader* loader = nullptr;
    IDWriteFontCollection* fonts = nullptr;
    IDWriteTypography* typography = nullptr;
    IDWriteInlineObject* ellipsis = nullptr;
    IDWriteTextFormat* formats[4] = {};
    float ascent = .96875f, descent = .2421875f; // per em, replaced by font metrics
    std::map<Glyph, ID2D1PathGeometry*> icons;   // bounded: five glyphs
    struct Entry { IDWriteTextLayout* layout = nullptr; };
    std::map<std::wstring, Entry> cache;         // bounded: CacheLimit entries
    static constexpr size_t CacheLimit = 48;
    ID2D1RenderTarget* target = nullptr;         // not owned
    ID2D1SolidColorBrush* brush = nullptr;       // bound to target

    ~Painter() { Dispose(); }
    void Dispose() {
        Unbind(); ClearCache();
        for (auto& i : icons) Drop(i.second);
        icons.clear();
        for (auto*& f : formats) Drop(f);
        Drop(ellipsis); Drop(typography); Drop(fonts);
        if (write && loader) write->UnregisterFontCollectionLoader(loader);
        Drop(loader); Drop(write); Drop(factory);
    }
    void ClearCache() { for (auto& e : cache) Drop(e.second.layout); cache.clear(); }
    HRESULT Init(const std::wstring& assetRoot) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
        if (SUCCEEDED(hr)) hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&write));
        if (FAILED(hr)) return hr;
        std::wstring folder = assetRoot;
        if (!folder.empty() && folder.back() != L'\\' && folder.back() != L'/') folder += L'\\';
        loader = new FontLoader(folder + L"fonts\\");
        hr = write->RegisterFontCollectionLoader(loader);
        const UINT key = 0x7057;
        if (SUCCEEDED(hr)) hr = write->CreateCustomFontCollection(loader, &key, sizeof(key), &fonts);
        if (FAILED(hr)) return hr;
        UINT32 index = 0; BOOL exists = FALSE;
        if (FAILED(fonts->FindFamilyName(L"Inter Display", &index, &exists)) || !exists) return E_FAIL;
        IDWriteFontFamily* family = nullptr; IDWriteFont* font = nullptr;
        if (SUCCEEDED(fonts->GetFontFamily(index, &family)) &&
            SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
            DWRITE_FONT_METRICS m{}; font->GetMetrics(&m);
            if (m.designUnitsPerEm) { ascent = (float)m.ascent / m.designUnitsPerEm; descent = (float)m.descent / m.designUnitsPerEm; }
        }
        Drop(font); Drop(family);
        hr = write->CreateTypography(&typography);
        if (FAILED(hr)) return hr;
        for (auto tag : {DWRITE_MAKE_OPENTYPE_TAG('c', 'p', 's', 'p'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '1', '2'), DWRITE_MAKE_OPENTYPE_TAG('t', 'n', 'u', 'm')})
            typography->AddFontFeature({(DWRITE_FONT_FEATURE_TAG)tag, 1});
        for (int r = 0; r < 4; ++r) {
            const RoleSpec s = Spec((Role)r);
            hr = write->CreateTextFormat(L"Inter Display", fonts, s.medium ? DWRITE_FONT_WEIGHT_MEDIUM : DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, s.size, L"en-us", &formats[r]);
            if (FAILED(hr)) return hr;
            formats[r]->SetWordWrapping(s.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
            formats[r]->SetTextAlignment(s.center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
            // CSS line box: half-leading above the font's ascent.
            const float baseline = (s.line - (ascent + descent) * s.size) / 2 + ascent * s.size;
            formats[r]->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, s.line, baseline);
        }
        hr = write->CreateEllipsisTrimmingSign(formats[(int)Role::Button], &ellipsis);
        if (FAILED(hr)) return hr;
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        formats[(int)Role::Button]->SetTrimming(&trim, ellipsis);
        for (Glyph g : {Glyph::Info, Glyph::Download, Glyph::CheckCircle, Glyph::ArrowDown, Glyph::Timer}) {
            ID2D1PathGeometry* path = nullptr;
            hr = BuildPath(factory, IconData(g).d, &path);
            if (FAILED(hr)) return hr;
            icons[g] = path;
        }
        return S_OK;
    }
    IDWriteTextLayout* Text(Role role, const std::wstring& text, float width) {
        wchar_t prefix[48]; std::swprintf(prefix, 48, L"%d|%.3f|", (int)role, width);
        const std::wstring key = prefix + text;
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.layout;
        if (cache.size() >= CacheLimit) ClearCache();
        IDWriteTextLayout* layout = nullptr;
        const RoleSpec s = Spec(role);
        if (FAILED(write->CreateTextLayout(text.c_str(), (UINT32)text.size(), formats[(int)role], std::max(1.f, width), s.line, &layout))) return nullptr;
        const DWRITE_TEXT_RANGE all{0, (UINT32)text.size()};
        if (s.features) layout->SetTypography(typography, all);
        IDWriteTextLayout1* layout1 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(&layout1)))) {
            layout1->SetCharacterSpacing(0, s.tracking * s.size, 0, all);
            layout1->Release();
        }
        cache[key].layout = layout;
        return layout;
    }
    int Lines(Role role, const std::wstring& text, float width) {
        IDWriteTextLayout* l = Text(role, text, width);
        DWRITE_TEXT_METRICS m{};
        if (!l || FAILED(l->GetMetrics(&m))) return 1;
        return (int)m.lineCount;
    }
    float NaturalWidth(Role role, const std::wstring& text) {
        IDWriteTextLayout* l = Text(role, text, 4096);
        DWRITE_TEXT_METRICS m{};
        if (!l || FAILED(l->GetMetrics(&m))) return 0;
        return m.widthIncludingTrailingWhitespace;
    }
    // CSS text-wrap: balance — narrowest width keeping the greedy line count.
    float BalancedWidth(const std::wstring& text, float width, int* lines) {
        const int n = Lines(Role::Body, text, width);
        if (lines) *lines = n;
        if (n <= 1) return width;
        float lo = width / n, hi = width;
        for (int i = 0; i < 10; ++i) {
            const float mid = std::floor((lo + hi) * .5f);
            if (mid <= lo || mid >= hi) break;
            if (Lines(Role::Body, text, mid) == n) hi = mid; else lo = mid;
        }
        return hi;
    }
    Geometry Measure(const Content& c, float width) {
        int lines = 0;
        if (!c.body.empty()) BalancedWidth(c.body, ContentWidth(width), &lines);
        return Layout(c, width, lines);
    }
    // Downloading stats: ETA is dropped when the row would not fit (narrow cards).
    bool EtaFits(const Content& c, float rowWidth) {
        if (c.eta.empty()) return false;
        const float need = Paper::statIcon + Paper::statGap + NaturalWidth(Role::Stat, c.size) + Paper::statGap +
                           Paper::statIcon + Paper::statGap + NaturalWidth(Role::Stat, c.eta);
        return need <= rowWidth + .01f;
    }

    void Unbind() { Drop(brush); target = nullptr; }
    HRESULT Bind(ID2D1RenderTarget* rt) {
        if (rt == target && brush) return S_OK;
        Unbind(); target = rt;
        return rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    }
    void Color(float gray, float alpha) { brush->SetColor(D2D1::ColorF(gray, gray, gray, alpha)); }
    void Icon(Glyph g, Rect r, float size, float alpha, float scale = 1) {
        auto it = icons.find(g);
        if (it == icons.end() || alpha <= 0) return;
        D2D1_MATRIX_3X2_F old; target->GetTransform(&old);
        const float k = size / IconData(g).view * scale;
        const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        target->SetTransform(D2D1::Matrix3x2F::Translation(-IconData(g).view / 2, -IconData(g).view / 2) *
                             D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(cx, cy) * old);
        Color(1, alpha);
        target->FillGeometry(it->second, brush);
        target->SetTransform(old);
    }
    void Label(Role role, const std::wstring& text, float x, float y, float width, float alpha) {
        IDWriteTextLayout* l = Text(role, text, width);
        if (!l || alpha <= 0) return;
        Color(1, alpha);
        target->DrawTextLayout(D2D1::Point2F(x, y), l, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
    void DrawLayer(const LayerFrame& lf, float dy, float progress, const ButtonVisual* visuals, bool reduced) {
        const Content& c = *lf.content; const Geometry& g = *lf.geometry;
        const float a = lf.opacity;
        if (a <= 0) return;
        Rect icon = g.icon; icon.top += dy; icon.bottom += dy;
        Icon(c.icon, icon, Paper::titleIcon, Paper::iconOpacity * a, lf.iconScale);
        // Title text box is the 14 DIP line box centred in the 16 DIP row.
        Label(Role::Title, c.title, g.title.left, g.title.top + dy + (Paper::titleRow - Paper::titleLine) / 2, 4096, Paper::textOpacity * a);
        if (g.body.Height() > 0) {
            const float w = BalancedWidth(c.body, g.body.Width(), nullptr);
            Label(Role::Body, c.body, g.body.left, g.body.top + dy, w, Paper::textOpacity * a);
        }
        if (c.stats) {
            const float y = g.stats.top + dy, rowH = 14;
            float x = g.stats.left;
            Icon(Glyph::ArrowDown, {x, y, x + Paper::statIcon, y + rowH}, Paper::statIcon, Paper::iconOpacity * a);
            x += Paper::statIcon + Paper::statGap;
            Label(Role::Stat, c.size, x, y, 4096, Paper::textOpacity * a);
            x += NaturalWidth(Role::Stat, c.size) + Paper::statGap;
            if (EtaFits(c, g.stats.Width())) {
                Icon(Glyph::Timer, {x, y, x + Paper::statIcon, y + rowH}, Paper::statIcon, Paper::iconOpacity * a);
                x += Paper::statIcon + Paper::statGap;
                Label(Role::Stat, c.eta, x, y, 4096, Paper::textOpacity * a);
            }
            const Rect t{g.track.left, g.track.top + dy, g.track.right, g.track.bottom + dy};
            const float r = Paper::track / 2;
            Color(1, Paper::trackOpacity * a);
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(t.left, t.top, t.right, t.bottom), r, r), brush);
            if (progress > 0) {
                const float w = std::max(Paper::track, t.Width() * std::min(progress, 1.f));
                Color(1, Paper::fillOpacity * a);
                target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(t.left, t.top, t.left + w, t.bottom), r, r), brush);
            }
        }
        for (size_t i = 0; i < c.buttons.size() && i < g.buttons.size(); ++i) {
            const ButtonVisual v = visuals ? visuals[std::min<size_t>(i, 1)] : ButtonVisual{};
            const auto style = NativeChrome::Design::Resolve(NativeChrome::Design::ButtonVariant::Raised, {v.hover, v.press, false, v.focus});
            Rect b = g.buttons[i]; b.top += dy; b.bottom += dy;
            const float scale = Motion::PressScale(v.press, reduced);
            D2D1_MATRIX_3X2_F old; target->GetTransform(&old);
            if (scale != 1) target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F((b.left + b.right) / 2, (b.top + b.bottom) / 2)) * old);
            Color(style.surface, a);
            const D2D1_ROUNDED_RECT shape = D2D1::RoundedRect(D2D1::RectF(b.left, b.top, b.right, b.bottom), Paper::buttonRadius, Paper::buttonRadius);
            target->FillRoundedRectangle(shape, brush);
            Label(Role::Button, c.buttons[i].label, b.left + Paper::buttonPadX, b.top + (Paper::buttonHeight - 14) / 2,
                     b.Width() - 2 * Paper::buttonPadX, Paper::buttonTextOpacity * a);
            if (v.focus) {
                const float s = NativeChrome::Design::Focus::stroke;
                Color(NativeChrome::Design::Focus::color, NativeChrome::Design::Focus::Opacity(v.keyboard) * a);
                target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b.left + s / 2, b.top + s / 2, b.right - s / 2, b.bottom - s / 2),
                                                               Paper::buttonRadius - s / 2, Paper::buttonRadius - s / 2), brush, s);
            }
            target->SetTransform(old);
        }
    }
    // Paints a sampled frame into the card box {0,0,width,height} (DIP) of the bound target.
    void Paint(const Frame& f, float width, float height, const ButtonVisual visuals[2], bool reduced, float materialOpacity = MaterialOpacity) {
        if (!f.visible || !brush) return;
        const bool fade = f.opacity < .999f;
        if (fade) target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                          D2D1::IdentityMatrix(), f.opacity), nullptr);
        const D2D1_ROUNDED_RECT card = D2D1::RoundedRect(D2D1::RectF(0, 0, width, height), Paper::radius, Paper::radius);
        Color(Paper::surface, materialOpacity);
        target->FillRoundedRectangle(card, brush);
        const float b = Paper::border;
        Color(1, Paper::borderOpacity);
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b / 2, b / 2, width - b / 2, height - b / 2), Paper::radius - b / 2, Paper::radius - b / 2), brush, b);
        // Content is anchored to the card's bottom edge; clip while heights differ.
        bool clip = false;
        for (const auto& l : f.layers) if (l.content && std::fabs(l.geometry->height - height) > .5f) clip = true;
        ID2D1RoundedRectangleGeometry* mask = nullptr;
        if (clip && SUCCEEDED(factory->CreateRoundedRectangleGeometry(card, &mask)))
            target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask), nullptr);
        for (int i = 0; i < 2; ++i) {
            const LayerFrame& l = f.layers[i];
            if (!l.content) continue;
            DrawLayer(l, height - l.geometry->height, f.progress, i == 1 ? visuals : nullptr, reduced);
        }
        if (mask) { target->PopLayer(); mask->Release(); }
        if (fade) target->PopLayer();
    }
};

}} // namespace BrowserUpdate::ToastDetail

namespace BrowserUpdate {
using namespace ToastDetail;

struct Toast::Impl {
    static constexpr UINT FrameMessage = WM_APP + 0x51, ActionMessage = WM_APP + 0x52;
    static constexpr UINT_PTR DwellTimer = 1, FallbackTimer = 2;
    HWND owner = nullptr, hwnd = nullptr, backdrop = nullptr;
    int backdropW = 0, backdropH = 0, backdropRadius = 0;
    Options options;
    ActionHandler onAction;
    std::function<void()> onRelease;
    Painter painter;
    Presenter presenter;
    Clicks clicks;
    Dwell dwell;
    NativeChrome::FrameClock::Clock clock;
    bool clockReady = false, fallbackFrames = false;
    Motion::Interaction interactions[2];
    View view{}; bool hasView = false;
    Content content;
    int hover = -1, focus = -1; bool keyboard = false, focused = false, spaceDown = false, tracking = false;
    bool ownerVisible = true, fits = true, windowShown = false, dismissedInstalled = false;
    float reservedRight = 0, width = Paper::width;
    UINT dpi = 96;
    // Last presented geometry (screen px) for hit testing.
    RECT card{}; float scale = 1, cardHeight = 0;
    // Layered surface (bounded: grows to the largest card only).
    HDC memory = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ oldBitmap = nullptr; int capW = 0, capH = 0;
    ID2D1DCRenderTarget* dc = nullptr;
    unsigned long long presents = 0;

    ~Impl() { Destroy(); }
    bool Reduced() const { return options.forceReducedMotion >= 0 ? options.forceReducedMotion != 0 : Motion::SystemReducedMotion(); }
    void Destroy() {
        clock.Stop(); clockReady = false;
        if (hwnd) { HWND h = hwnd; hwnd = nullptr; SetWindowLongPtrW(h, GWLP_USERDATA, 0); if (IsWindow(h)) DestroyWindow(h); }
        if (backdrop) { if (IsWindow(backdrop)) DestroyWindow(backdrop); backdrop = nullptr; }
        Drop(dc); painter.Unbind();
        if (memory) { if (oldBitmap) SelectObject(memory, oldBitmap); DeleteDC(memory); memory = nullptr; }
        if (bitmap) { DeleteObject(bitmap); bitmap = nullptr; }
        capW = capH = 0; windowShown = false;
    }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (self) { LRESULT r = 0; if (self->Handle(h, m, w, l, r)) return r; }
        return DefWindowProcW(h, m, w, l);
    }
    bool Create(HINSTANCE instance) {
        static ATOM atom = 0;
        if (!atom) {
            WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.lpszClassName = L"BrowserUpdateToast";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            atom = RegisterClassW(&wc);
            if (!atom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) atom = 1;
            if (!atom) return false;
        }
        hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"BrowserUpdateToast", L"",
                               WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (hwnd) CreateBackdrop(instance);
        return hwnd != nullptr;
    }
    // DWM cannot paint a system backdrop into an UpdateLayeredWindow surface.
    // Keep the acrylic in a separate, non-layered owned popup directly beneath
    // the transparent text/control surface. DWM samples the live page itself;
    // there is no page capture, bitmap polling or additional animation timer.
    static LRESULT CALLBACK BackdropProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCHITTEST) return HTTRANSPARENT;
        if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (m == WM_PAINT) {
            PAINTSTRUCT p; HDC dc = BeginPaint(h, &p);
            FillRect(dc, &p.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
            EndPaint(h, &p); return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    void CreateBackdrop(HINSTANCE instance) {
        WNDCLASSW wc{}; wc.lpfnWndProc = BackdropProc; wc.hInstance = instance;
        wc.lpszClassName = L"BrowserUpdateBackdrop";
        RegisterClassW(&wc);
        backdrop = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
            wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, nullptr);
        if (!backdrop) return;
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(backdrop, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        const DWM_SYSTEMBACKDROP_TYPE material = DWMSBT_TRANSIENTWINDOW;
        const MARGINS margins{-1, -1, -1, -1};
        if (FAILED(DwmExtendFrameIntoClientArea(backdrop, &margins)) ||
            FAILED(DwmSetWindowAttribute(backdrop, DWMWA_SYSTEMBACKDROP_TYPE, &material, sizeof(material)))) {
            DestroyWindow(backdrop); backdrop = nullptr; // Windows < 11 22H2: readable solid fallback
        }
    }
    bool PresentBackdrop(int w, int h, float opacity) {
        if (!backdrop) return false;
        if (opacity <= 1.f / 255) { ShowWindow(backdrop, SW_HIDE); return false; }
        const int radius = (int)std::lround(Paper::radius * scale);
        if (w != backdropW || h != backdropH || radius != backdropRadius) {
            HRGN clip = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius * 2, radius * 2);
            if (!clip || !SetWindowRgn(backdrop, clip, FALSE)) {
                if (clip) DeleteObject(clip);
                ShowWindow(backdrop, SW_HIDE); return false;
            }
            backdropW = w; backdropH = h; backdropRadius = radius;
        }
        if (!SetWindowPos(backdrop, hwnd, card.left, card.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            ShowWindow(backdrop, SW_HIDE); return false;
        }
        return true;
    }
    // ---- geometry ------------------------------------------------------------
    struct Client { RECT px{}; float scale = 1, w = 0, h = 0; };
    Client OwnerClient() const {
        Client c; RECT r{};
        if (!owner || !GetClientRect(owner, &r)) return c;
        MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&r), 2);
        // A maximised custom-frame window extends past the monitor: keep the card on the work area.
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) {
            RECT clipped{};
            if (IntersectRect(&clipped, &r, &mi.rcWork)) r = clipped;
        }
        c.px = r; c.scale = dpi / 96.f;
        c.w = (r.right - r.left) / c.scale; c.h = (r.bottom - r.top) / c.scale;
        return c;
    }
    void Refit(Motion::Time now) {
        if (owner) { const UINT d = GetDpiForWindow(owner); if (d) dpi = d; }
        const Client c = OwnerClient();
        const float w = FitWidth(c.w, options.edgeInset, reservedRight);
        const float next = w > 0 ? w : Paper::width;
        if (next != width) { width = next; if (presenter.Shown() && content.kind != Kind::None) presenter.Set(content, painter.Measure(content, width), now); }
        fits = w > 0 && (content.kind == Kind::None || FitsHeight(c.h, options.topReserve, painter.Measure(content, width).height));
    }
    // ---- frames ---------------------------------------------------------------
    void Request() {
        if (!hwnd) return;
        if (clockReady && clock.Running()) clock.Request();
        else if (!fallbackFrames) { fallbackFrames = true; SetTimer(hwnd, FallbackTimer, 16, nullptr); } // clock unavailable only
    }
    bool InteractionsMoving(Motion::Time now) const { return interactions[0].NextWake(now).delay || interactions[1].NextWake(now).delay; }
    void Tick() {
        const auto now = Motion::Now();
        presenter.Settle(now);
        Present(now);
        if (presenter.Moving(now) || InteractionsMoving(now)) Request();
        else if (fallbackFrames && hwnd) { KillTimer(hwnd, FallbackTimer); fallbackFrames = false; }
        ArmDwell(now);
    }
    void ArmDwell(Motion::Time now) {
        if (!hwnd) return;
        KillTimer(hwnd, DwellTimer);
        if (auto d = dwell.Deadline()) {
            const auto ms = std::chrono::ceil<Ms>(*d - now).count();
            SetTimer(hwnd, DwellTimer, (UINT)std::max<long long>(USER_TIMER_MINIMUM, ms), nullptr);
        }
    }
    bool EnsureSurface(int w, int h) {
        if (w <= capW && h <= capH && memory && dc) return true;
        const int cw = std::max(w, capW), ch = std::max(h, capH);
        if (!memory) memory = CreateCompatibleDC(nullptr);
        if (!memory) return false;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = cw; info.bmiHeader.biHeight = -ch; info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP next = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!next) return false;
        HGDIOBJ previous = SelectObject(memory, next);
        if (!oldBitmap) oldBitmap = previous;
        if (bitmap) DeleteObject(bitmap);
        bitmap = next; capW = cw; capH = ch;
        if (!dc) {
            const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            if (FAILED(painter.factory->CreateDCRenderTarget(&props, &dc))) return false;
            dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // ClearType needs an opaque surface
        }
        return true;
    }
    ButtonVisual Visual(int i, Motion::Time now) const {
        ButtonVisual v; const auto iv = interactions[i].Visual(now);
        v.hover = iv.hover; v.press = iv.press; v.focus = focused && focus == i; v.keyboard = keyboard;
        return v;
    }
    void Hide() {
        if (backdrop) ShowWindow(backdrop, SW_HIDE);
        if (hwnd && windowShown) { ShowWindow(hwnd, SW_HIDE); windowShown = false; }
    }
    void Present(Motion::Time now) {
        if (!hwnd) return;
        const Frame f = presenter.Sample(now);
        if (!f.visible || !ownerVisible || !fits || IsIconic(owner)) {
            Hide();
            if (!f.visible && focused) ReleaseFocus();
            return;
        }
        const Client c = OwnerClient();
        scale = c.scale;
        const int left = c.px.left + (int)std::lround(options.edgeInset * scale);
        const int bottom = c.px.bottom - (int)std::lround(EdgeBottom * scale) + (int)std::lround(f.offset * scale);
        const int w = (int)std::lround(width * scale), h = std::max(1, (int)std::lround(f.height * scale));
        card = {left, bottom - h, left + w, bottom}; cardHeight = h / scale;
        if (!EnsureSurface(w, h)) return;
        RECT bind{0, 0, w, h};
        if (FAILED(dc->BindDC(memory, &bind))) return;
        dc->SetDpi((float)dpi, (float)dpi);
        if (FAILED(painter.Bind(dc))) return;
        const bool acrylic = PresentBackdrop(w, h, f.opacity);
        dc->BeginDraw();
        dc->Clear(D2D1::ColorF(0, 0, 0, 0));
        const ButtonVisual visuals[2] = {Visual(0, now), Visual(1, now)};
        painter.Paint(f, width, cardHeight, visuals, presenter.Reduced(), acrylic ? Paper::surfaceOpacity : MaterialOpacity);
        const HRESULT hr = dc->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { painter.Unbind(); Drop(dc); Request(); return; }
        POINT dst{card.left, card.top}, src{0, 0}; SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, nullptr, &dst, &size, memory, &src, 0, &blend, ULW_ALPHA);
        ++presents;
        if (!windowShown) {
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            windowShown = true;
        }
    }
    // ---- input ----------------------------------------------------------------
    // Card-local DIP of a screen point, or false when outside the visible rounded card.
    bool Local(POINT screen, float& x, float& y) const {
        if (!windowShown || !presenter.Interactive()) return false;
        x = (screen.x - card.left) / scale; y = (screen.y - card.top) / scale;
        const float w = width, h = cardHeight, r = Paper::radius;
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        const float cx = std::clamp(x, r, w - r), cy = std::clamp(y, r, h - r);
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
    }
    int ButtonAt(POINT screen) const {
        float x, y;
        if (!Local(screen, x, y)) return -1;
        const Geometry& g = presenter.CurrentGeometry();
        const float dy = cardHeight - g.height;
        for (size_t i = 0; i < g.buttons.size(); ++i)
            if (g.buttons[i].Contains(x, y - dy)) return (int)i;
        return -1;
    }
    void SetInteraction(Motion::Time now) {
        const int pressed = clicks.Pressed(presenter.Generation());
        for (int i = 0; i < 2; ++i) interactions[i].Set(hover == i, pressed == i || (spaceDown && focus == i), now, presenter.Reduced());
        dwell.Hold(hover >= 0 || focused || tracking, now);
        Request();
    }
    void Fire(std::optional<Action> action) {
        if (action && hwnd) PostMessageW(hwnd, ActionMessage, (WPARAM)*action, 0);
    }
    void ReleaseFocus() {
        const bool was = focused;
        focused = false; focus = -1; keyboard = false; spaceDown = false;
        if (!was) return;
        if (onRelease) onRelease();
        else if (owner && IsWindow(owner)) { SetActiveWindow(owner); SetFocus(owner); }
    }
    int Buttons() const { return (int)presenter.Current().buttons.size(); }
    bool Handle(HWND h, UINT m, WPARAM w, LPARAM l, LRESULT& result) {
        const auto now = Motion::Now();
        switch (m) {
        case WM_NCHITTEST: {
            float x, y; POINT p{(short)LOWORD(l), (short)HIWORD(l)};
            result = Local(p, x, y) ? HTCLIENT : HTTRANSPARENT; return true;
        }
        case WM_MOUSEACTIVATE: result = MA_NOACTIVATE; return true;
        case WM_MOUSEMOVE: {
            if (!tracking) { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; tracking = TrackMouseEvent(&t) != FALSE; }
            POINT p{(short)LOWORD(l), (short)HIWORD(l)}; ClientToScreen(h, &p);
            const int at = ButtonAt(p);
            if (at != hover) { hover = at; SetInteraction(now); }
            return true;
        }
        case WM_MOUSELEAVE: tracking = false; hover = -1; SetInteraction(now); return true;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
            POINT p{(short)LOWORD(l), (short)HIWORD(l)}; ClientToScreen(h, &p);
            const int at = ButtonAt(p);
            if (at >= 0) { clicks.Down(at, presenter.Generation()); SetCapture(h); }
            if (focused) keyboard = false;
            SetInteraction(now); return true;
        }
        case WM_LBUTTONUP: {
            POINT p{(short)LOWORD(l), (short)HIWORD(l)}; ClientToScreen(h, &p);
            const int at = presenter.Interactive() ? ButtonAt(p) : -1;
            const auto action = clicks.Up(at, presenter.Generation(), presenter.Current());
            if (GetCapture() == h) ReleaseCapture();
            SetInteraction(now); Fire(action); return true;
        }
        case WM_CAPTURECHANGED: clicks.Cancel(); SetInteraction(now); return true;
        case WM_SETFOCUS: focused = true; if (focus < 0 && Buttons() > 0) focus = Buttons() - 1; SetInteraction(now); return true;
        case WM_KILLFOCUS: focused = false; spaceDown = false; SetInteraction(now); return true;
        case WM_KEYDOWN: {
            const int n = Buttons();
            keyboard = true;
            if (w == VK_TAB || w == VK_LEFT || w == VK_RIGHT || w == VK_UP || w == VK_DOWN) {
                if (n > 0) {
                    const bool back = w == VK_LEFT || w == VK_UP || (w == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
                    focus = focus < 0 ? (back ? n - 1 : 0) : (focus + (back ? n - 1 : 1)) % n;
                }
                SetInteraction(now); return true;
            }
            if (w == VK_RETURN) { Fire(clicks.Activate(focus, presenter.Generation(), presenter.Current())); return true; }
            if (w == VK_SPACE) { spaceDown = focus >= 0; SetInteraction(now); return true; }
            if (w == VK_ESCAPE) {
                if (presenter.Current().kind == Kind::Installed) Fire(Action::Dismiss);
                ReleaseFocus(); SetInteraction(now); return true;
            }
            return false;
        }
        case WM_KEYUP:
            if (w == VK_SPACE && spaceDown) { spaceDown = false; SetInteraction(now); Fire(clicks.Activate(focus, presenter.Generation(), presenter.Current())); return true; }
            return false;
        case WM_TIMER:
            if (w == DwellTimer) {
                KillTimer(h, DwellTimer);
                if (dwell.Expired(now)) { dwell.Stop(); Fire(Action::Dismiss); }
                else ArmDwell(now);
                return true;
            }
            if (w == FallbackTimer) { Tick(); return true; }
            return false;
        case WM_SETTINGCHANGE:
            if (w == SPI_SETCLIENTAREAANIMATION || w == 0) { presenter.SetReduced(Reduced(), now); Request(); }
            return false;
        case WM_DPICHANGED: Refit(now); Request(); result = 0; return true; // owner DPI governs; ignore suggestion
        case WM_NCDESTROY:
            clock.Stop(); clockReady = false;
            if (hwnd == h) hwnd = nullptr;
            windowShown = false; return false;
        default:
            if (m == FrameMessage) {
                clock.Acknowledge();
                if (l == NativeChrome::FrameClock::ClockLost) { clockReady = false; Request(); return true; }
                Tick(); return true;
            }
            if (m == ActionMessage) {
                const Action a = (Action)w;
                if (a == Action::Dismiss && presenter.Current().kind == Kind::Installed) {
                    // Success clears itself locally; the backend is told once.
                    dwell.Stop(); dismissedInstalled = true; presenter.Set({}, {}, now); Request();
                }
                ActionHandler handler = onAction; // invoked last: it may Close() or destroy the toast
                if (handler) handler(a);
                return true;
            }
            return false;
        }
    }
    // ---- API ------------------------------------------------------------------
    void SetView(const View& v) {
        const auto now = Motion::Now();
        if (hasView && v.phase == view.phase && v.received == view.received && v.total == view.total &&
            v.secondsRemaining == view.secondsRemaining && v.version == view.version && v.error == view.error) return;
        const bool phaseChanged = !hasView || v.phase != view.phase;
        view = v; hasView = true;
        if (phaseChanged) dismissedInstalled = false;
        Content next = Describe(v);
        if (next.kind == Kind::Installed && dismissedInstalled) next = {};
        presenter.SetReduced(Reduced(), now);
        content = next;
        Refit(now);
        presenter.Set(next, next.kind == Kind::None ? Geometry{} : painter.Measure(next, width), now);
        if (phaseChanged) {
            clicks.Cancel(); focus = focused && Buttons() > 0 ? Buttons() - 1 : -1;
            if (next.kind == Kind::Installed) dwell.Start(now); else dwell.Stop();
        }
        if (hwnd) SetWindowTextW(hwnd, next.kind == Kind::None ? L"" : (next.title + L". " + next.body).c_str());
        SetInteraction(now);
        Request();
        ArmDwell(now);
    }
};

Toast::Toast() = default;
Toast::~Toast() { Close(); }
bool Toast::Init(HWND owner, const std::wstring& assetRoot, ActionHandler onAction, Options options) {
    Close();
    auto impl = std::make_unique<Impl>();
    impl->owner = owner; impl->options = options; impl->onAction = std::move(onAction);
    if (!owner || !IsWindow(owner) || FAILED(impl->painter.Init(assetRoot))) return false;
    impl->presenter.SetReduced(impl->Reduced(), NativeChrome::Motion::Now());
    if (!impl->Create((HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE))) return false;
    impl->clockReady = impl->clock.Start(impl->hwnd, Impl::FrameMessage, options.allowCompositorClock);
    impl->Refit(NativeChrome::Motion::Now());
    impl_ = std::move(impl);
    return true;
}
void Toast::SetView(const View& view) { if (impl_) impl_->SetView(view); }
void Toast::Layout() { if (impl_) { impl_->Refit(NativeChrome::Motion::Now()); impl_->Present(NativeChrome::Motion::Now()); impl_->Request(); } }
void Toast::SetOwnerVisible(bool visible) {
    if (!impl_) return;
    impl_->ownerVisible = visible;
    if (!visible) { impl_->clock.Cancel(); impl_->Hide(); if (impl_->hwnd) KillTimer(impl_->hwnd, Impl::FallbackTimer); impl_->fallbackFrames = false; }
    else Layout();
}
void Toast::SetReservedRight(float dip) { if (impl_) { impl_->reservedRight = std::max(0.f, dip); Layout(); } }
bool Toast::Focus() {
    if (!impl_ || !impl_->hwnd || !impl_->windowShown || !impl_->presenter.Interactive()) return false;
    impl_->keyboard = true;
    SetActiveWindow(impl_->hwnd);
    SetFocus(impl_->hwnd);
    return GetFocus() == impl_->hwnd;
}
void Toast::SetFocusReleaseHandler(std::function<void()> handler) { if (impl_) impl_->onRelease = std::move(handler); }
void Toast::Close() { impl_.reset(); }
HWND Toast::Window() const { return impl_ ? impl_->hwnd : nullptr; }
bool Toast::Shown() const { return impl_ && impl_->windowShown; }
bool Toast::Fits() const { return impl_ && impl_->fits; }
} // namespace BrowserUpdate
