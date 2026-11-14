#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/profile-color.h"
#include "../../src/chrome.h"
using namespace NativeChrome;
void Require(bool condition,const char* what){if(!condition){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
// Accent states (user report 2026-11-14: hover went back to the legacy blue): rest / hover / pressed derived from
// the fill on screen; hue kept; white text >= 4.5:1 in every state and every interpolation, for every profile fill.
static int AccentStateFailures() {
    namespace A = NativeChrome::Design::Accent; int bad = 0;
    auto rgb = [](ProfileColor::Rgb8 c) { return NativeChrome::Design::Rgb{c.r / 255.f, c.g / 255.f, c.b / 255.f}; };
    auto hueOf = [](NativeChrome::Design::Rgb c) { const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn; if (d < 1e-4f) return -1.f;
        float h = mx == c.r ? std::fmod((c.g - c.b) / d, 6.f) : mx == c.g ? (c.b - c.r) / d + 2 : (c.r - c.g) / d + 4; h *= 60; return h < 0 ? h + 360 : h; };
    float worst = 99, worstHueShift = 0;
    for (int hue = 0; hue < 360; ++hue) for (float chroma : {.03f, .06f, .10f, .14f, .16f}) {
        const auto fill = rgb(ProfileColor::AccentFill(hue, chroma)); const auto st = A::StatesOf(fill);
        for (int h = 0; h <= 10; ++h) for (int q = 0; q <= 10; ++q) { const float c = A::WhiteContrast(A::StateColor(fill, h / 10.f, q / 10.f)); worst = std::min(worst, c); if (c < A::minWhiteContrast) ++bad; }
        const float h0 = hueOf(st.rest);
        for (auto x : {st.hover, st.pressed}) { const float h1 = hueOf(x); if (h0 >= 0 && h1 >= 0) { float dh = std::fabs(h1 - h0); dh = std::min(dh, 360 - dh); worstHueShift = std::max(worstHueShift, dh); if (dh > 1.f) ++bad; } }
        if (!(A::Luminance(st.hover) < A::Luminance(st.rest) && A::Luminance(st.pressed) < A::Luminance(st.hover))) ++bad;   // visible feedback, rest > hover > pressed
    }
    std::printf("%s accent states: 360 hues x 5 chromas x 121 hover/press mixes, worst white contrast %.2f:1 (>= 4.5), worst hue shift %.2f deg\n", bad ? "FAIL" : "ok  ", worst, worstHueShift);
    // Vivid profiles: green / orange / purple never come near the legacy blue pair in any state.
    for (int hue : {145, 55, 305}) {
        const auto st = A::StatesOf(rgb(ProfileColor::AccentFill(hue, .14f)));
        const float legacy = hueOf({0x57 / 255.f, 0x79 / 255.f, 1});   // the legacy hover blue hue (~227 deg)
        float nearest = 360; for (auto x : {st.rest, st.hover, st.pressed}) { float dh = std::fabs(hueOf(x) - legacy); dh = std::min(dh, 360 - dh); nearest = std::min(nearest, dh); }
        if (nearest < 20) ++bad;
        std::printf("%s OKLCH hue %d: every state at sRGB hue %.0f deg, %.0f deg away from the legacy blue\n", nearest < 20 ? "FAIL" : "ok  ", hue, hueOf(st.hover), nearest);
    }
    // Neutral profile: the reference blue fill at rest exactly; hover / press deepen it (the legacy #5779FF fails 4.5:1).
    const NativeChrome::Design::Rgb ref{0x3E / 255.f, 0x67 / 255.f, 1};
    const auto n = A::StatesOf(ref);
    const bool exact = n.rest.r == ref.r && n.rest.g == ref.g && n.rest.b == ref.b;
    const bool legacyFails = A::WhiteContrast({0x57 / 255.f, 0x79 / 255.f, 1}) < 4.5f;
    std::printf("%s neutral: rest #3E67FF exactly, hover %.2f:1 / pressed %.2f:1, legacy hover #5779FF %.2f:1 (fails 4.5, replaced)\n", exact && legacyFails && A::WhiteContrast(n.hover) >= 4.5f ? "ok  " : "FAIL", A::WhiteContrast(n.hover), A::WhiteContrast(n.pressed), A::WhiteContrast({0x57 / 255.f, 0x79 / 255.f, 1}));
    if (!exact || !legacyFails) ++bad;
    return bad;
}

int wmain(int argc,wchar_t** argv){
    Require(argc==3,"root and output arguments");Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"native COM initialization");
    Require(!GetModuleHandleW(L"WebView2Loader.dll"),"no WebView2 loader");
    Require(AccentStateFailures()==0,"accent states: derived from the fill on screen, hue kept, white >= 4.5:1 everywhere");
    using namespace Design;
    for(auto variant:{ButtonVariant::Ghost,ButtonVariant::Raised,ButtonVariant::Embedded,ButtonVariant::Window,ButtonVariant::TabClose}){
        auto idle=Resolve(variant),hover=Resolve(variant,{1,0}),press=Resolve(variant,{1,1}),disabled=Resolve(variant,{1,1,true,true}),focus=Resolve(variant,{0,0,false,true});
        Require(disabled.surface==idle.surface&&!disabled.focus&&Motion::PressScale(1,false,false)==1,"disabled ignores hover, press and focus");
        Require(disabled.iconOpacity<idle.iconOpacity,"disabled is visibly unavailable");
        Require(focus.focus&&focus.surface==idle.surface,"keyboard focus independent of fill");
        Require(idle.radius==Resolve(ButtonVariant::Ghost).radius&&idle.radius==Radius::button,"every button variant shares the Downloads (Ghost) radius");
        Require(std::abs(Motion::PressScale(1)-.96f)<.0001f,"pressed scale endpoint");
        if(variant!=ButtonVariant::TabClose)Require(press.surface>=hover.surface&&hover.surface>=idle.surface,"monotonic opaque interaction fills");
    }
    Require(VariantFor('X')==VariantFor('m')&&VariantFor('M')==VariantFor('m'),"identical window control variant");
    for(UINT dpi:{96,144,192}){
        Chrome chrome;Require(SUCCEEDED(chrome.InitOffscreen(argv[1],dpi,1180,560)),"production offscreen renderer");
        // Verify real glyph coverage and OpenType features on production layouts.
        for(bool medium:{false,true}){
            IDWriteTextLayout* layout=nullptr;TextMeasure measure;
            Require(SUCCEEDED(chrome.MakeTextLayout(L"\u00e9\u00e8\u00e0\u00f9\u00c9\u00c8\u00c0\u00d9e\u0301a\u0300",{0,0,500,32},14,false,medium,0,&layout,measure)),"accent layout");
            IDWriteFontCollection* collection=nullptr;Require(SUCCEEDED(layout->GetFontCollection(0,&collection)),"layout font collection");
            Require(collection->GetFontFamilyCount()==1,"only Inter Display required");
            IDWriteFontFamily* family=nullptr;IDWriteFont* font=nullptr;IDWriteFontFace* face=nullptr;
            Require(SUCCEEDED(collection->GetFontFamily(0,&family)),"Inter family");
            Require(SUCCEEDED(family->GetFirstMatchingFont(medium?DWRITE_FONT_WEIGHT_MEDIUM:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STRETCH_NORMAL,DWRITE_FONT_STYLE_NORMAL,&font)),"Inter weight");
            Require(SUCCEEDED(font->CreateFontFace(&face)),"Inter face");
            UINT32 points[]={0xe9,0xe8,0xe0,0xf9,0xc9,0xc8,0xc0,0xd9,0x301,0x300};UINT16 glyphs[10]{};
            Require(SUCCEEDED(face->GetGlyphIndices(points,10,glyphs)),"French glyph lookup");
            for(auto glyph:glyphs)Require(glyph!=0,"accent has a real glyph, not a missing-glyph box");
            Drop(face);Drop(font);Drop(family);Drop(collection);Drop(layout);
        }
        for(bool tabular:{false,true}){
            IDWriteTextLayout* layout=nullptr;TextMeasure measure;
            Require(SUCCEEDED(chrome.CachedTextLayout(L"0123456789",{0,0,500,32},14,tabular,false,0,&layout,measure)),"cached digit layout");
            IDWriteTypography* typography=nullptr;Require(SUCCEEDED(layout->GetTypography(0,&typography)),"layout typography");
            bool found=false;for(UINT32 i=0;i<typography->GetFontFeatureCount();++i){DWRITE_FONT_FEATURE feature{};typography->GetFontFeature(i,&feature);if(feature.nameTag==DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES)found=feature.parameter==1;}
            Require(found==tabular,"tabular figures only for explicit counters, including cache");Drop(typography);Drop(layout);
        }
        Require(SUCCEEDED(chrome.PaintStateSheet()),"production state sheet render");
        Require(SUCCEEDED(chrome.SavePng((std::wstring(argv[2])+L"/components-"+std::to_wstring(dpi)+L".png").c_str())),"state sheet PNG export");
        chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7"},0,L"https://example.test",false,CloseBase,0);
        auto selected=chrome.SurfacePixel(TabStrip::Surface(0).right-2,16);
        chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7"},0,L"https://example.test",false,TabBase,0);
        Require(selected==chrome.SurfacePixel(TabStrip::Surface(0).right-2,16),"close hover has single tab fill");
        chrome.SetEnabled('R',false);chrome.Build(2,0);Require(chrome.Hit(Toolbar::reloadLeft+16,60)==0,"disabled control excluded from pointer activation");
        auto order=chrome.FocusOrder();Require(std::find(order.begin(),order.end(),'R')==order.end(),"disabled excluded from keyboard order");auto tab=std::find(order.begin(),order.end(),TabBase);Require(tab!=order.end()&&tab+1!=order.end()&&*(tab+1)==CloseBase,"tab body precedes its reserved close action in keyboard order");
        chrome.SetEnabled('R',true);Require(chrome.Hit(Toolbar::reloadLeft+16,60)=='R',"enabled control restores same geometry");
        auto m=chrome.MeasureText(L"Ggyp \u00c9\u00e7",{0,0,200,32},Type::label.size,true);Require(m.inkTop>=0&&m.inkBottom<=32,"complete glyph ink retained");
        chrome.keyboardModality=false;chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7"},0,L"https://example.test",false,0,'L');const int pointerRing=chrome.SurfacePixel(chrome.bar.left,(chrome.bar.top+chrome.bar.bottom)/2)&255;
        Require(SUCCEEDED(chrome.SavePng((std::wstring(argv[2])+L"/chrome-focus-pointer-"+std::to_wstring(dpi)+L".png").c_str())),"production chrome pointer focus snapshot");
        chrome.keyboardModality=true;chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7"},0,L"https://example.test",false,0,'L');const int keyboardRing=chrome.SurfacePixel(chrome.bar.left,(chrome.bar.top+chrome.bar.bottom)/2)&255;
        Require(keyboardRing>pointerRing+40,"keyboard focus ring is markedly stronger than pointer focus ring");
        chrome.Paint({L"Google",L"Ggyp \u00c9\u00e7"},0,L"https://example.test",false,0,'R');
        Require(SUCCEEDED(chrome.SavePng((std::wstring(argv[2])+L"/chrome-focus-"+std::to_wstring(dpi)+L".png").c_str())),"production chrome focus snapshot");
        {   // Decision 7: « Privée » chip (Paper LRZ-0) before the tabs, measured label, tabs shifted by it.
            const float normalFirst=chrome.strip.Surface(0).left;const int normalCapacity=Design::TabStrip::Capacity(1180);
            chrome.privateChip=true;chrome.Paint({L"Google",L"Ggyp Éç"},0,L"https://example.test",false,0,0);
            const auto c=chrome.PrivateChipRect();const float label=chrome.TextWidth(Design::PrivateChip::label,14,true);
            Require(c.left==44&&c.top==10&&c.bottom==34&&c.right-c.left==6+16+6+std::ceil(label)+8,"private chip: x 44, y 10, h 24, width 6+16+6+label+8 from DirectWrite");
            Require(std::abs(label-39)<3,"private chip: « Privée » measures about Paper's 39 DIP");
            Require(chrome.strip.Surface(0).left==c.right+6&&normalFirst==44,"private chip: first tab one 6 DIP row gap after the chip");
            Require(Design::TabStrip::Capacity(1180)<=normalCapacity&&Design::TabStrip::Capacity(500)>=1,"private chip: capacity accounts for it, still a tab at narrow widths");
            Require(SUCCEEDED(chrome.SavePng((std::wstring(argv[2])+L"/chrome-private-"+std::to_wstring(dpi)+L".png").c_str())),"private chip snapshot");
            chrome.privateChip=false;chrome.Paint({L"Google",L"Ggyp Éç"},0,L"https://example.test",false,0,0);
            Require(chrome.strip.Surface(0).left==44&&Design::TabStrip::leading==0,"normal window: tabs back at x 44");
        }
    }
    Chrome animated;Require(SUCCEEDED(animated.InitOffscreen(argv[1],96,1180,84)),"animated production fixture");
    Motion::State motion(false);motion.EnablePolish();animated.SetMotion(&motion);animated.animateSelection=true;
    const auto start=Motion::Time{};motion.SelectTab(1,start);motion.SelectTab(2,start+Motion::Milliseconds(10));
    int before=0,after=0;for(int ms:{10,70,130}){animated.Paint({L"First",L"Second"},1,L"",false,0,0,nullptr,start+Motion::Milliseconds(ms));int old=animated.SurfacePixel(TabStrip::Surface(0).right-2,16)&255,newer=animated.SurfacePixel(TabStrip::Surface(1).right-2,16)&255;if(ms==10){before=old;Require(newer==23,"new selection starts at existing canvas without entrance jump");}if(ms==130){after=old;Require(newer==41,"selected surface reaches semantic selected endpoint");}Require(old>=23&&old<=41&&newer>=23&&newer<=41,"selection interpolation bounded to opaque surface tokens");}
    Require(before==41&&after==23,"old tab selection surface fades to canvas");
    motion.SetReducedMotion(true);motion.SelectTab(1,start+Motion::Milliseconds(140));animated.Paint({L"First",L"Second"},0,L"",false,0,0,nullptr,start+Motion::Milliseconds(140));Require((animated.SurfacePixel(TabStrip::Surface(0).right-2,16)&255)==41,"reduced motion selection updates immediately");
    animated.Dispose();Require(!GetModuleHandleW(L"WebView2Loader.dll"),"native fixture never loaded engine");
    std::puts("PASS: typed variants, disabled/focus isolation, pointer vs keyboard focus strength, opaque state fills, native state sheets at 96/144/192 DPI, stable close hover and glyph ink.");CoUninitialize();return 0;
}
