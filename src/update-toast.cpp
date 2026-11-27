// Native update card renderer + one owned opaque popup. See update-toast.h and
// build/coordination/update-toast-contract.md. No WebView2, no browser main.
//
// Val's « Mise à jour » card (Paper « Atelier de Val », 2026-11-26; values in design/update-toast.md):
//   background  Paper Shaders' GrainGradient (src/onboarding-shaders.h, Direct3D 11, read back as a Direct2D bitmap —
//               the onboarding's and the Réglages card's existing way of drawing an animated Paper shader), 30 frames
//               a second only while someone can see it move; one still frame with reduced motion or a software device;
//               nothing at all (no frame, no timer, no GPU memory) while the card is hidden.
//   fade        #17171700 -> #171717 at 79.64 % over 347 DIP, a Direct2D linear gradient.
//   icon        static Phosphor paths; while downloading, the Downloads drawer's own Lottie frames and model
//               (src/download-icon.h) composed in white at 85 %; its end ripple; the check grows to Val's check-circle.
//   texts       Inter Display (the shipped files), DirectWrite, CSS line boxes.
#include "update-toast.h"
#include "update-toast-motion.h"
#include "frame-clock.h"
#include "motion-presentation.h"
#include "design-system.h"
#include "native-ui.h"
#include "native-lottie.h"
#include "shader-layer.h"
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite_1.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <cstdlib>
#include <map>

namespace BrowserUpdate { namespace ToastDetail {
using namespace ToastModel;
namespace Motion = NativeChrome::Motion;
namespace Lottie = NativeChrome::Lottie;
static_assert(Paper::buttonRadius == NativeChrome::Design::Radius::button, "card actions share the Downloads button radius");
template <class T> inline void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Phosphor Bold path data (MIT, assets/icons/phosphor-bold/LICENSE): the Paper boards' SVGs at 256 units.
enum class PathId { Tray, CheckCircle, Info, ArrowDown, Timer, CheckLine };
struct IconPath { const char* d; float view; };
inline IconPath IconData(PathId g) {
    switch (g) {
    case PathId::Tray: return {"M228,144v64a12,12,0,0,1-12,12H40a12,12,0,0,1-12-12V144a12,12,0,0,1,24,0v52H204V144a12,12,0,0,1,24,0Zm-108.49,8.49a12,12,0,0,0,17,0l40-40a12,12,0,0,0-17-17L140,115V32a12,12,0,0,0-24,0v83L96.49,95.51a12,12,0,0,0-17,17Z", 256};
    case PathId::CheckCircle: return {"M176.49,95.51a12,12,0,0,1,0,17l-56,56a12,12,0,0,1-17,0l-24-24a12,12,0,1,1,17-17L112,143l47.51-47.52A12,12,0,0,1,176.49,95.51ZM236,128A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Z", 256};
    case PathId::Info: return {"M108,84a16,16,0,1,1,16,16A16,16,0,0,1,108,84Zm128,44A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Zm-72,36.68V132a20,20,0,0,0-20-20,12,12,0,0,0-4,23.32V168a20,20,0,0,0,20,20,12,12,0,0,0,4-23.32Z", 256};
    case PathId::ArrowDown: return {"M91.215 66.714l-31.5 31.5a5.25 5.25 0 0 1-7.437 0l-31.5-31.5a5.25 5.25 0 0 1 7.437-7.437L50.751 81.812V17.5a5.25 5.25 0 0 1 10.5 0V81.812l22.536-22.54a5.25 5.25 0 0 1 7.437 7.438Z", 112};
    case PathId::Timer: return {"M56.001 19.25a42 42 0 1 0 42 42A42.048 42.048 90 0 0 56.001 19.25Zm0 73.5a31.5 31.5 0 1 1 31.5-31.5A31.535 31.535 0 0 1 56.001 92.75ZM71.965 43.535a5.25 5.25 0 0 1 0 7.438l-12.25 12.25a5.25 5.25 0 0 1-7.437-7.438l12.25-12.25A5.25 5.25 0 0 1 71.965 43.535ZM40.251 7A5.25 5.25 0 0 1 45.501 1.75h21a5.25 5.25 0 0 1 0 10.5H45.501A5.25 5.25 0 0 1 40.251 7Z", 112};
    // The success check's centreline (design/lottie/download-success/download-success.py CHECK), stroked 24 round.
    case PathId::CheckLine: return {"M88,136L112,160L168,104", 256};
    }
    return {"", 1};
}

// Minimal SVG path parser (M L H V C A Z, absolute and relative, implicit repeats).
inline HRESULT BuildPath(ID2D1Factory* factory, const char* d, ID2D1PathGeometry** out, bool filled = true) {
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
    auto begin = [&](D2D1_POINT_2F at) { if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN); sink->BeginFigure(at, filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW); open = true; start = at; };
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

// Paper text styles: title Medium 16/14 −0.01 em, text Regular 14/18 +0.01 em (centred, balanced), button labels
// Medium 14/14 −0.01 em (no features), progress line Regular 14/14 −0.01 em. Features cpsp cv12 tnum except buttons.
enum class Role { Title, Body, Button, Stat };
struct RoleSpec { bool medium; float size, line, tracking; bool features, wrap, center; };
inline RoleSpec Spec(Role r) {
    switch (r) {
    case Role::Title: return {true, Paper::titleSize, Paper::titleLine, Paper::titleTracking, true, false, true};
    case Role::Body: return {false, Paper::bodySize, Paper::bodyLine, Paper::bodyTracking, true, true, true};
    case Role::Button: return {true, Paper::buttonSize, Paper::buttonLine, Paper::buttonTracking, false, false, true};
    case Role::Stat: return {false, Paper::statSize, Paper::statLine, Paper::statTracking, true, false, false};
    }
    return {};
}

// Button visuals for the two action slots ([0] primary, [1] secondary).
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
    ID2D1StrokeStyle* round = nullptr;
    float ascent = .96875f, descent = .2421875f; // per em, replaced by font metrics
    std::map<PathId, ID2D1PathGeometry*> icons;  // bounded: six paths
    struct Entry { IDWriteTextLayout* layout = nullptr; };
    std::map<std::wstring, Entry> cache;         // bounded: CacheLimit entries
    static constexpr size_t CacheLimit = 64;
    // The Downloads drawer's frames (assets/lottie, the toolbar's own files): track, fill, arrow, cut, success, success cut.
    Lottie::Asset track, fill, arrow, cut, success, successCut;
    bool drawerReady = false;
    std::vector<uint32_t> pixels;
    struct IconSlot { ID2D1Bitmap* bitmap = nullptr; unsigned side = 0; DI::Pose pose; float dim = -1; };
    IconSlot slots[2];                           // [0] the card's icon, [1] the outgoing icon of a cross-fade
    unsigned long long composes = 0;             // test hook: drawer frames composed
    ID2D1RenderTarget* target = nullptr;         // not owned
    ID2D1SolidColorBrush* brush = nullptr;       // bound to target
    ID2D1LinearGradientBrush* fadeBrush = nullptr;
    ID2D1DeviceContext* rippleDc = nullptr; ID2D1Effect* rippleBlur = nullptr; ID2D1BitmapRenderTarget* rippleLayer = nullptr;
    float rippleExtent = 0; bool rippleNoEffect = false;

    ~Painter() { Dispose(); }
    void Dispose() {
        Unbind(); ClearCache();
        for (auto& i : icons) Drop(i.second);
        icons.clear();
        for (auto*& f : formats) Drop(f);
        Drop(round); Drop(ellipsis); Drop(typography); Drop(fonts);
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
        formats[(int)Role::Title]->SetTrimming(&trim, ellipsis);   // a title never runs past the card padding
        formats[(int)Role::Button]->SetTrimming(&trim, ellipsis);
        formats[(int)Role::Stat]->SetTrimming(&trim, ellipsis);
        for (PathId g : {PathId::Tray, PathId::CheckCircle, PathId::Info, PathId::ArrowDown, PathId::Timer, PathId::CheckLine}) {
            ID2D1PathGeometry* path = nullptr;
            hr = BuildPath(factory, IconData(g).d, &path, g != PathId::CheckLine);
            if (FAILED(hr)) return hr;
            icons[g] = path;
        }
        hr = factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &round);
        if (FAILED(hr)) return hr;
        // The drawer's frames at the card's size (up to 256 px a side, rendered on demand: one frame of memory each).
        Lottie::Limits limits; limits.cacheFrames = false; limits.maxSide = 256;
        const std::wstring lottie = folder + L"lottie\\";
        drawerReady = track.Load(lottie + L"download-ring-track.json", limits) && fill.Load(lottie + L"download-ring-fill.json", limits) &&
                      arrow.Load(lottie + L"download-arrow.json", limits) && cut.Load(lottie + L"download-arrow-cutout.json", limits) &&
                      success.Load(lottie + L"download-success.json", limits) && successCut.Load(lottie + L"download-success-cutout.json", limits) &&
                      track.FrameCount() == DI::RingFrames && fill.FrameCount() == DI::RingFrames && arrow.FrameCount() == DI::ArrowFrames &&
                      cut.FrameCount() == DI::ArrowFrames && success.FrameCount() == DI::SuccessFrames && successCut.FrameCount() == DI::SuccessFrames;
        return S_OK;   // without the drawer's frames the download shows the static tray (as the toolbar falls back)
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
    // The text as Paper (Chromium's text-wrap: balance) breaks it. Two lines: split at the space where the longer line is
    // shortest; a near tie (under half a DIP) goes to the earlier break, as Paper does: « Une nouvelle mise à jour / est
    // prête à être téléchargée. » (176.15 / 176.20 DIP here — the other split would win by 0.05 DIP). More lines: the
    // narrowest width that keeps the greedy count (BalancedWidth). Returns the text with its line break.
    std::wstring Balanced(const std::wstring& text, float width) {
        if (Lines(Role::Body, text, width) != 2) return text;
        struct Split { size_t at; float longest; };
        std::vector<Split> splits;
        for (size_t at = text.find(L' '); at != std::wstring::npos; at = text.find(L' ', at + 1)) {
            const float a = NaturalWidth(Role::Body, text.substr(0, at)), b = NaturalWidth(Role::Body, text.substr(at + 1));
            if (a <= width && b <= width) splits.push_back({at, std::max(a, b)});
        }
        if (splits.empty()) return text;
        float best = splits[0].longest;
        for (const auto& s : splits) best = std::min(best, s.longest);
        for (const auto& s : splits) if (s.longest <= best + .5f) return text.substr(0, s.at) + L"\n" + text.substr(s.at + 1);
        return text;
    }
    float SecondaryWidth(const Content& c) {
        if (c.buttons.size() < 2) return 0;
        return std::ceil(NaturalWidth(Role::Button, c.buttons[1].label) * 64) / 64 + 2 * Paper::secondaryPadX;
    }
    Geometry Measure(const Content& c, float width) {
        int lines = 0;
        if (!c.body.empty()) BalancedWidth(c.body, ContentWidth(width), &lines);
        return Layout(c, width, lines, SecondaryWidth(c));
    }

    void DropSlots() { for (auto& s : slots) { Drop(s.bitmap); s = IconSlot{}; } }
    void DropRipple() { Drop(rippleLayer); Drop(rippleBlur); Drop(rippleDc); rippleExtent = 0; rippleNoEffect = false; }
    void Unbind() { DropSlots(); DropRipple(); Drop(fadeBrush); Drop(brush); target = nullptr; }
    HRESULT Bind(ID2D1RenderTarget* rt) {
        if (rt == target && brush) return S_OK;
        Unbind(); target = rt;
        HRESULT hr = rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
        if (FAILED(hr)) return hr;
        const D2D1_GRADIENT_STOP stops[] = {{0, D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 0)},
                                            {Paper::fadeStop, D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 1)},
                                            {1, D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 1)}};
        ID2D1GradientStopCollection* collection = nullptr;
        hr = rt->CreateGradientStopCollection(stops, 3, &collection);
        if (SUCCEEDED(hr)) hr = rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, Paper::fadeHeight}), collection, &fadeBrush);
        Drop(collection);
        return hr;
    }
    void Color(float gray, float alpha) { brush->SetColor(D2D1::ColorF(gray, gray, gray, alpha)); }
    // A Phosphor path in a `size` box whose top-left is (x, y).
    void FillPath(PathId g, float x, float y, float size, float alpha) {
        auto it = icons.find(g);
        if (it == icons.end() || alpha <= 0) return;
        D2D1_MATRIX_3X2_F old; target->GetTransform(&old);
        const float k = size / IconData(g).view;
        target->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y) * old);
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
    // ---- icon ------------------------------------------------------------------
    // The drawer's frame for `pose` in white (ComposeWhite), at the target's pixel density.
    ID2D1Bitmap* DrawerBitmap(IconSlot& slot, const DI::Pose& pose, float dim) {
        float dx = 96, dy = 96; target->GetDpi(&dx, &dy);
        const unsigned side = std::clamp(static_cast<unsigned>(std::lround(Paper::icon * dx / 96)), 1u, 256u);
        if (slot.bitmap && slot.side == side && slot.pose == pose && slot.dim == dim) return slot.bitmap;
        auto at = [&](Lottie::Asset& a, int frame, int frames) { return a.Sample(double(frame) / (frames - 1), side); };
        const bool done = pose.success >= 0;
        const Lottie::Frame* t = at(track, pose.ring, DI::RingFrames);
        const Lottie::Frame* f = at(fill, pose.ring, DI::RingFrames);
        const Lottie::Frame* a = done ? at(success, pose.success, DI::SuccessFrames) : at(arrow, pose.arrow, DI::ArrowFrames);
        const Lottie::Frame* c = done ? at(successCut, pose.success, DI::SuccessFrames) : at(cut, pose.arrow, DI::ArrowFrames);
        if (!t || !f || !a || !c) return nullptr;
        ComposeWhite(t->bgra.data(), f->bgra.data(), c->bgra.data(), a->bgra.data(), side * side, pixels, Paper::iconOpacity, dim);
        ++composes;
        HRESULT hr = S_OK;
        if (slot.bitmap && slot.side != side) Drop(slot.bitmap);
        if (!slot.bitmap) hr = target->CreateBitmap(D2D1::SizeU(side, side), pixels.data(), side * 4,
                                                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy), &slot.bitmap);
        else hr = slot.bitmap->CopyFromMemory(nullptr, pixels.data(), side * 4);
        if (FAILED(hr)) { Drop(slot.bitmap); return nullptr; }
        slot.side = side; slot.pose = pose; slot.dim = dim;
        return slot.bitmap;
    }
    void DrawIcon(const IconFrame& fr, const Rect& box, int slot) {
        if (fr.opacity <= 0 || fr.mode == IconFrame::Mode::None) return;
        const float ink = Paper::iconOpacity * fr.opacity;
        if (fr.mode == IconFrame::Mode::Drawer && drawerReady) {
            if (ID2D1Bitmap* b = DrawerBitmap(slots[slot], fr.pose, fr.dim)) {
                // Whole device pixels nearest to Paper's place (the frame is a bitmap; a fractional offset would blur it).
                float dx = 96, dy = 96; target->GetDpi(&dx, &dy);
                const float s = dx / 96, x = std::round((box.left + Paper::trayDx) * s) / s, y = std::round((box.top + Paper::trayDy) * s) / s;
                target->DrawBitmap(b, D2D1::RectF(x, y, x + Paper::icon, y + Paper::icon), fr.opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                return;
            }
        }
        if (fr.mode == IconFrame::Mode::Check) {
            // The drawer's ring (r 88) and its check grow into Val's check-circle (r 96), the fill back to the ink.
            const float k = Motion::Clamp(fr.settle), u = Paper::icon / 256;
            const float x = box.left + Paper::trayDx + (Paper::checkDx - Paper::trayDx) * k, y = box.top + Paper::trayDy + (Paper::checkDy - Paper::trayDy) * k;
            D2D1_MATRIX_3X2_F old; target->GetTransform(&old);
            target->SetTransform(D2D1::Matrix3x2F::Scale(u, u) * D2D1::Matrix3x2F::Translation(x, y) * old);
            Color(1, ink * (1 - .5f * k * (1 - k)));
            const float r = 88 + 8 * k;
            target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(128, 128), r, r), brush, 24, round);
            Color(1, ink);
            target->DrawGeometry(icons[PathId::CheckLine], brush, 24, round);
            target->SetTransform(old);
            return;
        }
        const Glyph g = fr.mode == IconFrame::Mode::Static ? fr.glyph : Glyph::Tray;
        if (g == Glyph::Tray || g == Glyph::Drawer) FillPath(PathId::Tray, box.left + Paper::trayDx, box.top + Paper::trayDy, Paper::icon, ink);
        else FillPath(g == Glyph::Check ? PathId::CheckCircle : PathId::Info, box.left + Paper::checkDx, box.top + Paper::checkDy, Paper::icon, ink);
    }
    // The drawer's end ripple (src/chrome.h DrawDownloadRipple, src/download-icon.h Ripple): white, under the texts.
    void DrawRipple(float t, D2D1_POINT_2F centre) {
        if (t < 0 || t >= 1) return;
        namespace R = DI::Ripple;
        const float p = Motion::Eased(t, Motion::Curve::Icon), unit = Paper::icon / 256.f, scale = 1 + (R::grow - 1) * p;
        const float radius = R::radius * unit * scale, width = R::stroke * unit * scale, blur = R::blur * unit * scale * p, alpha = R::opacity * (1 - p);
        Color(1, alpha);
        if (!rippleNoEffect && !rippleDc) {
            static const CLSID gaussian = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
            if (FAILED(target->QueryInterface(__uuidof(ID2D1DeviceContext), reinterpret_cast<void**>(&rippleDc))) || FAILED(rippleDc->CreateEffect(gaussian, &rippleBlur))) { Drop(rippleBlur); Drop(rippleDc); rippleNoEffect = true; }
        }
        if (rippleDc && rippleBlur && blur > .05f) {
            const float extent = 2 * R::reach * unit + 2;
            if (!rippleLayer || rippleExtent != extent) {
                Drop(rippleLayer);
                if (FAILED(target->CreateCompatibleRenderTarget(D2D1::SizeF(extent, extent), &rippleLayer))) { rippleLayer = nullptr; rippleNoEffect = true; }
                else rippleExtent = extent;
            }
            if (rippleLayer) {
                ID2D1SolidColorBrush* ink = nullptr;
                rippleLayer->BeginDraw(); rippleLayer->Clear(D2D1::ColorF(0, 0, 0, 0));
                if (SUCCEEDED(rippleLayer->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, alpha), &ink))) rippleLayer->DrawEllipse(D2D1::Ellipse({extent / 2, extent / 2}, radius, radius), ink, width);
                Drop(ink);
                ID2D1Bitmap* bitmap = nullptr;
                if (SUCCEEDED(rippleLayer->EndDraw()) && SUCCEEDED(rippleLayer->GetBitmap(&bitmap)) && bitmap) {
                    rippleBlur->SetInput(0, bitmap);
                    const float deviation = blur; const UINT32 soft = 0; // D2D1_BORDER_MODE_SOFT
                    rippleBlur->SetValue(0, D2D1_PROPERTY_TYPE_FLOAT, reinterpret_cast<const BYTE*>(&deviation), sizeof(deviation));
                    rippleBlur->SetValue(2, D2D1_PROPERTY_TYPE_ENUM, reinterpret_cast<const BYTE*>(&soft), sizeof(soft));
                    ID2D1Image* output = nullptr; rippleBlur->GetOutput(&output);
                    if (output) {
                        const D2D1_POINT_2F at{centre.x - extent / 2, centre.y - extent / 2};
                        rippleDc->DrawImage(output, &at, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
                        output->Release();
                    }
                    rippleBlur->SetInput(0, nullptr); bitmap->Release();
                    return;
                }
                if (bitmap) bitmap->Release();
            }
        }
        target->DrawEllipse(D2D1::Ellipse(centre, radius, radius), brush, width);
    }
    // ---- texts and actions ----------------------------------------------------------
    void DrawTexts(const LayerFrame& lf) {
        const Content& c = *lf.content; const Geometry& g = *lf.geometry;
        const float a = lf.opacity;
        if (a <= 0) return;
        Label(Role::Title, c.title, g.title.left, g.title.top, g.title.Width(), Paper::titleOpacity * a);
        if (g.body.Height() > 0) {
            if (g.bodyLines == 2) Label(Role::Body, Balanced(c.body, g.body.Width()), g.body.left, g.body.top, g.body.Width(), Paper::bodyOpacity * a);
            else {
                const float w = BalancedWidth(c.body, g.body.Width(), nullptr);
                Label(Role::Body, c.body, g.body.left + (g.body.Width() - w) / 2, g.body.top, w, Paper::bodyOpacity * a);
            }
        }
        if (c.stats && g.stats.Height() > 0) {
            // Paper's progress line, centred: ↓ + size, gap 4, timer + remaining time. The timer group waits until the
            // time is known (the engine's first second, an unknown total): never invented.
            const bool timed = !c.eta.empty();
            const float ws = NaturalWidth(Role::Stat, c.size), we = timed ? NaturalWidth(Role::Stat, c.eta) : 0;
            const float row = Paper::statIcon + Paper::statGap + ws + (timed ? Paper::statGap + Paper::statIcon + Paper::statGap + we : 0);
            float x = g.stats.left + (g.stats.Width() - row) / 2;
            const float y = g.stats.top, alpha = Paper::statOpacity * a;
            FillPath(PathId::ArrowDown, x, y, Paper::statIcon, alpha);
            x += Paper::statIcon + Paper::statGap;
            Label(Role::Stat, c.size, x, y, ws + 1, alpha);
            if (timed) {
                x += ws + Paper::statGap;
                FillPath(PathId::Timer, x, y, Paper::statIcon, alpha);
                x += Paper::statIcon + Paper::statGap;
                Label(Role::Stat, c.eta, x, y, we + 1, alpha);
            }
        }
    }
    void DrawActions(const Frame& f, float width, float height, const ButtonVisual visuals[2], bool reduced) {
        if (f.actions <= 0) return;
        const float top = height - Paper::border - (Paper::padBottom - Paper::textTrim) - Paper::buttonHeight;
        const float x0 = Paper::border + Paper::padX, x1 = width - x0;
        const float second = f.secondaryWidth * f.secondary, gap = Paper::buttonGap * f.secondary;
        const Rect slots[2] = {{x0, top, x1 - second - gap, top + Paper::buttonHeight}, {x1 - second, top, x1, top + Paper::buttonHeight}};
        for (int i = 0; i < 2; ++i) {
            const Rect& b = slots[i];
            if (b.Width() < .5f) continue;
            const ButtonVisual v = visuals ? visuals[i] : ButtonVisual{};
            const auto style = NativeChrome::Design::Resolve(NativeChrome::Design::ButtonVariant::Raised, {v.hover, v.press, false, v.focus});
            const float scale = Motion::PressScale(v.press, reduced);
            D2D1_MATRIX_3X2_F old; target->GetTransform(&old);
            if (scale != 1) target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F((b.left + b.right) / 2, (b.top + b.bottom) / 2)) * old);
            const float r = std::min(style.radius, b.Width() / 2);
            Color(style.surface, f.actions);
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b.left, b.top, b.right, b.bottom), r, r), brush);
            target->PushAxisAlignedClip(D2D1::RectF(b.left, b.top, b.right, b.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (const LayerFrame& l : f.layers) {
                if (!l.content || l.opacity <= 0 || (int)l.content->buttons.size() <= i) continue;
                const std::wstring& label = l.content->buttons[i].label;
                const float y = b.top + (Paper::buttonHeight - Paper::buttonLine) / 2;
                if (i == 0) Label(Role::Button, label, b.left + Paper::primaryPadX, y, b.Width() - 2 * Paper::primaryPadX, Paper::primaryLabel * l.opacity * f.actions);
                else {   // the secondary folds: its label keeps its width, centred, clipped by the slot
                    const float w = NaturalWidth(Role::Button, label) + 1;
                    Label(Role::Button, label, (b.left + b.right - w) / 2, y, w, Paper::secondaryLabel * l.opacity * f.actions);
                }
            }
            target->PopAxisAlignedClip();
            if (v.focus) {
                const float s = NativeChrome::Design::Focus::stroke;
                Color(NativeChrome::Design::Focus::color, NativeChrome::Design::Focus::Opacity(v.keyboard) * f.actions);
                target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b.left + s / 2, b.top + s / 2, b.right - s / 2, b.bottom - s / 2), r - s / 2, r - s / 2), brush, s);
            }
            target->SetTransform(old);
        }
    }
    // Paints a sampled frame into the card box {0,0,width,height} (DIP) of the bound target. `grain`: the shader's
    // bitmap for this target (null: none yet / no GPU, the card's #171717 shows).
    void Paint(const Frame& f, float width, float height, const ButtonVisual visuals[2], bool reduced, ID2D1Bitmap* grain = nullptr) {
        if (!f.visible || !brush) return;
        const bool faded = f.opacity < .999f;
        if (faded) target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                           D2D1::IdentityMatrix(), f.opacity), nullptr);
        const D2D1_ROUNDED_RECT card = D2D1::RoundedRect(D2D1::RectF(0, 0, width, height), Paper::radius, Paper::radius);
        Color(Paper::surface, MaterialOpacity);
        target->FillRoundedRectangle(card, brush);
        // Everything inside the border is clipped to the padding box (Paper: overflow clip).
        const float b = Paper::border;
        ID2D1RoundedRectangleGeometry* inner = nullptr;
        const bool clipped = SUCCEEDED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(b, b, width - b, height - b), Paper::radius - b, Paper::radius - b), &inner));
        if (clipped) target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), inner), nullptr);
        if (grain) target->DrawBitmap(grain, D2D1::RectF(b, b, b + (width - b), b + Paper::grainHeight), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (fadeBrush) {
            const float top = b + f.fadeTop, left = b + f.fadeLeft;
            fadeBrush->SetStartPoint(D2D1::Point2F(0, top)); fadeBrush->SetEndPoint(D2D1::Point2F(0, top + Paper::fadeHeight));
            target->FillRectangle(D2D1::RectF(left, top, left + width, top + Paper::fadeHeight), fadeBrush);
        }
        const Rect& box = f.layers[1].geometry ? f.layers[1].geometry->icon : Rect{width / 2 - Paper::icon / 2, b + Paper::iconTop, width / 2 + Paper::icon / 2, b + Paper::iconTop + Paper::icon};
        const float unit = Paper::icon / 256;
        DrawRipple(f.ripple, D2D1::Point2F(box.left + Paper::trayDx + 128 * unit, box.top + Paper::trayDy + 128 * unit));
        DrawIcon(f.ghost, box, 1);
        DrawIcon(f.icon, box, 0);
        for (const LayerFrame& l : f.layers) if (l.content) DrawTexts(l);
        DrawActions(f, width, height, visuals, reduced);
        if (clipped) target->PopLayer();
        Drop(inner);
        Color(1, Paper::borderOpacity);
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b / 2, b / 2, width - b / 2, height - b / 2), Paper::radius - b / 2, Paper::radius - b / 2), brush, b);
        if (faded) target->PopLayer();
    }
};

}} // namespace BrowserUpdate::ToastDetail

namespace BrowserUpdate {
using namespace ToastDetail;

struct Toast::Impl {
    static constexpr UINT FrameMessage = WM_APP + 0x51, ActionMessage = WM_APP + 0x52;
    static constexpr UINT_PTR DwellTimer = 1, FallbackTimer = 2, GrainTimer = 3;
    static constexpr UINT GrainIntervalMs = 33;   // the background's 30 frames a second (as the Réglages card's shader)
    HWND owner = nullptr, hwnd = nullptr;
    Options options;
    ActionHandler onAction;
    std::function<void()> onRelease;
    std::wstring assetRoot;
    Painter painter;
    Presenter presenter;
    Clicks clicks;
    Dwell dwell;
    NativeChrome::FrameClock::Clock clock;
    bool clockReady = false, fallbackFrames = false, grainTimer = false;
    Motion::Interaction interactions[2];
    View view{}; bool hasView = false;
    Content content;
    int hover = -1, focus = -1; bool keyboard = false, focused = false, spaceDown = false, tracking = false;
    bool ownerVisible = true, fits = true, windowShown = false, dismissedInstalled = false;
    float reservedRight = 0, width = Paper::width;
    UINT dpi = 96;
    // Last presented geometry (screen px) for hit testing.
    RECT card{}; float scale = 1, cardHeight = 0;
    // One opaque HWND surface contains the complete card.
    ID2D1HwndRenderTarget* surface = nullptr;
    unsigned long long presents = 0, grainRenders = 0;
    // The GrainGradient: created when the card first shows, released when it has left (no GPU memory while hidden).
    Onboarding::Shaders::Renderer gpu; Onboarding::Shaders::Layer grain; bool gpuTried = false;
    int slowRenders = 0; bool grainSlow = false;   // GrainFrames::SlowMs guard (reset when the card leaves)
    double grainSeconds = 0; Motion::Time grainTick{};
    IWICImagingFactory* wic = nullptr;
    // The background's colours: from the window's profile ink, re-rendered when they change.
    std::array<ProfileColor::Rgb8, 3> grainColors{}; bool grainColored = false;
    ProfileColor::Rgb8 Ink() const {
        const auto ink = NativeChrome::Design::Accent::Ink();
        auto byte = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
        return {byte(ink.r), byte(ink.g), byte(ink.b)};
    }

    ~Impl() { Destroy(); }
    bool Reduced() const { return options.forceReducedMotion >= 0 ? options.forceReducedMotion != 0 : Motion::PreferredReducedMotion(); }
    void Destroy() {
        clock.Stop(); clockReady = false;
        if (hwnd) { HWND h = hwnd; hwnd = nullptr; SetWindowLongPtrW(h, GWLP_USERDATA, 0); if (IsWindow(h)) DestroyWindow(h); }
        ReleaseGrain(); painter.Unbind(); Drop(surface); Drop(wic);
        windowShown = false;
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
        hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"BrowserUpdateToast", L"",
                               WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (hwnd) ConfigureWindow();
        return hwnd != nullptr;
    }
    void ConfigureWindow() {
        NativeUI::ConfigureShadowlessPopup(hwnd, Paper::radius); // region-rounded: DWM rounding always casts a shadow
        const DWM_SYSTEMBACKDROP_TYPE material = DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &material, sizeof(material));
    }
    bool Place(int w, int h) {
        RECT previous{}; GetWindowRect(hwnd, &previous);
        if (!EqualRect(&previous, &card) &&
            !SetWindowPos(hwnd, nullptr, card.left, card.top, w, h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW)) return false;
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
    // Someone can see the card: the owner is up, not minimised, not cloaked (another virtual desktop, a hidden profile).
    bool OwnerSeen() const {
        if (!ownerVisible || !owner || !IsWindowVisible(owner) || IsIconic(owner)) return false;
        BOOL cloaked = FALSE;
        return FAILED(DwmGetWindowAttribute(owner, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || !cloaked;
    }
    // The background moves only while it can be seen, motion is allowed and a hardware device draws it.
    bool GrainLoops() const { return windowShown && !presenter.Reduced() && gpu.Animated() && !grainSlow && OwnerSeen(); }
    void ArmGrain(bool on) {
        if (!hwnd || on == grainTimer) return;
        if (on) grainTimer = SetTimer(hwnd, GrainTimer, GrainIntervalMs, nullptr) != 0;
        else { KillTimer(hwnd, GrainTimer); grainTimer = false; }
    }
    void ReleaseGrain() { ArmGrain(false); grain.Drop(); gpu.Dispose(); gpuTried = false; grainTick = {}; slowRenders = 0; grainSlow = false; }
    void Tick() {
        const auto now = Motion::Now();
        presenter.Settle(now);
        Present(now);
        const bool moving = presenter.Moving(now) || InteractionsMoving(now);
        if (moving) Request();
        else if (fallbackFrames && hwnd) { KillTimer(hwnd, FallbackTimer); fallbackFrames = false; }
        ArmGrain(!moving && GrainLoops());   // vsync frames already redraw it while the card moves
        if (!presenter.Shown()) ReleaseGrain();
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
        const auto size = D2D1::SizeU(w, h);
        if (!surface) {
            const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
            if (FAILED(painter.factory->CreateHwndRenderTarget(props,
                D2D1::HwndRenderTargetProperties(hwnd, size, D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS), &surface))) return false;
            surface->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        } else {
            const auto previous = surface->GetPixelSize();
            if ((previous.width != size.width || previous.height != size.height) && FAILED(surface->Resize(size))) return false;
        }
        return true;
    }
    // A new background frame when due: the first one, a new size or target, then 30 a second while it loops.
    void Shade(Motion::Time now) {
        if (!gpuTried) {
            gpuTried = true;
            if (!wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
            Onboarding::Shaders::Image noise;
            if (gpu.Init() && Onboarding::Shaders::LoadPng(wic, assetRoot + L"\\onboarding\\noise.png", noise)) gpu.SetInput(Onboarding::Shaders::Kind::GrainGradient, noise);
            else gpu.Dispose();
        }
        if (!gpu.Ready() || !gpu.HasInput(Onboarding::Shaders::Kind::GrainGradient)) return;
        const float cssW = width - Paper::border, cssH = Paper::grainHeight;
        const unsigned ow = static_cast<unsigned>(std::lround(cssW * scale)), oh = static_cast<unsigned>(std::lround(cssH * scale));
        const auto colors = Grain::Colors(Ink());
        const bool recolor = !grainColored || colors != grainColors;
        const bool current = grain.bitmap && grain.owner == surface && grain.frame.width == ow && grain.frame.height == oh && !recolor;
        const bool loops = GrainLoops();
        if (loops) {
            if (grainTick != Motion::Time{}) grainSeconds += std::min(.25, std::chrono::duration<double>(now - grainTick).count());
            grainTick = now;
        } else grainTick = {};   // paused (hidden, minimised, reduced motion): the wave stays where it is
        if (!GrainFrames::Due(current, loops, grain.shaded, now)) return;
        auto hex = [](ProfileColor::Rgb8 c) { return (static_cast<unsigned long>(c.r) << 16) | (static_cast<unsigned long>(c.g) << 8) | c.b; };
        const unsigned long rgb[3] = {hex(colors[0]), hex(colors[1]), hex(colors[2])};
        const auto started = std::chrono::steady_clock::now();
        if (gpu.Render(Onboarding::Shaders::Kind::GrainGradient, Onboarding::Shaders::UpdateCardGrain(grainSeconds, rgb), cssW, cssH, ow, oh, grain.frame)) {
            grain.Upload(surface); grain.shaded = now; ++grainRenders; grainColors = colors; grainColored = true;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (loops) {   // a background that costs too much stops moving (until the card leaves): never a load on the browser's thread
            slowRenders = ms > GrainFrames::SlowMs ? slowRenders + 1 : 0;
            if (slowRenders >= GrainFrames::SlowStrikes) { grainSlow = true; ArmGrain(false); }
        }
    }
    ButtonVisual Visual(int i, Motion::Time now) const {
        ButtonVisual v; const auto iv = interactions[i].Visual(now);
        v.hover = iv.hover; v.press = iv.press; v.focus = focused && focus == i; v.keyboard = keyboard;
        return v;
    }
    void Hide() {
        ArmGrain(false); grainTick = {};
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
        if (!Place(w, h) || !EnsureSurface(w, h)) { Hide(); return; }
        surface->SetDpi((float)dpi, (float)dpi);
        if (FAILED(painter.Bind(surface))) return;
        Shade(now);
        surface->BeginDraw();
        surface->Clear(D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 1.f));
        const ButtonVisual visuals[2] = {Visual(0, now), Visual(1, now)};
        painter.Paint(f, width, cardHeight, visuals, presenter.Reduced(), grain.owner == surface ? grain.bitmap : nullptr);
        const HRESULT hr = surface->EndDraw();
        if (FAILED(hr)) { grain.Drop(); painter.Unbind(); Drop(surface); Hide(); Request(); return; }
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
        const float dy = cardHeight - g.height;   // the button row is anchored to the card's bottom edge
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
        case WM_ERASEBKGND: result = 1; return true;
        case WM_PAINT: {
            PAINTSTRUCT p; BeginPaint(h, &p); EndPaint(h, &p);
            if (windowShown) Present(now);
            return true;
        }
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
        case WM_SETFOCUS: focused = true; if (focus < 0 && Buttons() > 0) focus = 0; SetInteraction(now); return true;
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
            if (w == FallbackTimer || w == GrainTimer) { Tick(); return true; }
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
            clicks.Cancel(); focus = focused && Buttons() > 0 ? 0 : -1;
            if (next.kind == Kind::Installed) dwell.Start(now); else dwell.Stop();
        }
        if (hwnd) SetWindowTextW(hwnd, next.kind == Kind::None ? L"" : (next.title + L". " + (next.body.empty() ? next.size : next.body)).c_str());
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
    impl->owner = owner; impl->options = options; impl->onAction = std::move(onAction); impl->assetRoot = assetRoot;
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
void Toast::SyncReducedMotion() { if (impl_) { impl_->presenter.SetReduced(impl_->Reduced(), NativeChrome::Motion::Now()); impl_->Request(); } }
void Toast::SetReservedRight(float dip) { if (impl_) { impl_->reservedRight = std::max(0.f, dip); Layout(); } }
void Toast::SetTopReserve(float dip) { if (impl_ && impl_->options.topReserve != dip) { impl_->options.topReserve = std::max(0.f, dip); Layout(); } }
bool Toast::Focus() {
    if (!impl_ || !impl_->hwnd || !impl_->windowShown || !impl_->presenter.Interactive()) return false;
    impl_->keyboard = true;
    SetActiveWindow(impl_->hwnd);
    SetFocus(impl_->hwnd);
    return GetFocus() == impl_->hwnd;
}
void Toast::SetFocusReleaseHandler(std::function<void()> handler) { if (impl_) impl_->onRelease = std::move(handler); }
void Toast::AccentChanged() { if (impl_ && impl_->windowShown) impl_->Request(); }
void Toast::Close() { impl_.reset(); }
HWND Toast::Window() const { return impl_ ? impl_->hwnd : nullptr; }
bool Toast::Shown() const { return impl_ && impl_->windowShown; }
bool Toast::Fits() const { return impl_ && impl_->fits; }
} // namespace BrowserUpdate
