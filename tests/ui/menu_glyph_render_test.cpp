// Split-view layout glyphs of the context menu (Paper 9Z2-0 « Menu V2 », comment c-bu5dzt):
// the outline and the half fill must read as one opaque #8A8A8A shape, so the strip where the
// fill covers the outline is not brighter than the outline alone (the former translucent ink
// doubled its alpha there). Off-screen WIC render at 100 / 125 / 150 %, no window, no WebView2.
// Usage: menu_glyph_render_test.exe [output dir for PNG renders]
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <string>
#include "../../src/context-menu.h"

namespace CM = NativeChrome::ContextMenu;
static int failures = 0;
static void Expect(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

struct Canvas {
    IWICImagingFactory* wic = nullptr; ID2D1Factory* d2d = nullptr; IWICBitmap* bitmap = nullptr; ID2D1RenderTarget* target = nullptr; ID2D1SolidColorBrush* brush = nullptr;
    UINT w = 0, h = 0; float scale = 1;
    Canvas(float dpi, UINT dipW, UINT dipH) {
        scale = dpi / 96.f; w = (UINT)(dipW * scale + .5f); h = (UINT)(dipH * scale + .5f);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
        wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
        d2d->CreateWicBitmapRenderTarget(bitmap, props, &target);
        target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    }
    ~Canvas() { if (brush) brush->Release(); if (target) target->Release(); if (bitmap) bitmap->Release(); if (d2d) d2d->Release(); if (wic) wic->Release(); }
    // Grey level of the pixel at (x, y) DIP (opaque background, so B == G == R).
    int At(float x, float y) {
        WICRect r{(INT)(x * scale), (INT)(y * scale), 1, 1}; BYTE px[4]{};
        bitmap->CopyPixels(&r, 4, 4, px); return px[1];
    }
    void Save(const std::wstring& path) {
        IWICStream* stream = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* frame = nullptr;
        if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return;
        wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc); enc->Initialize(stream, WICBitmapEncoderNoCache);
        enc->CreateNewFrame(&frame, nullptr); frame->Initialize(nullptr); frame->WriteSource(bitmap, nullptr); frame->Commit(); enc->Commit();
        frame->Release(); enc->Release(); stream->Release();
    }
};

// The pre-fix drawing (translucent white, outline then fill), kept here only to show the defect.
static void PaintOld(Canvas& c, CM::Glyph g, float x, float cy, float alpha) {
    c.brush->SetColor(D2D1::ColorF(1, 1, 1, alpha));
    const D2D1_RECT_F box{x, cy - CM::Layout::glyphH / 2, x + CM::Layout::glyphW, cy + CM::Layout::glyphH / 2};
    c.target->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .75f, box.top + .75f, box.right - .75f, box.bottom - .75f}, 1.25f, 1.25f), c.brush, 1.5f);
    D2D1_RECT_F fill = box; if (g == CM::Glyph::Right) fill.left = box.right - CM::Layout::glyphW / 2;
    c.target->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE); c.target->FillRoundedRectangle(D2D1::RoundedRect(fill, 2, 2), c.brush); c.target->PopAxisAlignedClip();
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring out = argc > 1 ? argv[1] : L"";
    const D2D1_COLOR_F menu = D2D1::ColorF(0x17 / 255.f, 0x17 / 255.f, 0x17 / 255.f, 1);
    // Highlighted row: #FFFFFF14 over the #171717 menu.
    const float hi = (0x17 + (255 - 0x17) * (0x14 / 255.f)) / 255.f;
    const D2D1_COLOR_F row = D2D1::ColorF(hi, hi, hi, 1);
    Expect(CM::Layout::glyphInk == 0x8A, "ink is Paper's #8A8A8A (9Z2-0 outline and fill)");
    for (float dpi : {96.f, 120.f, 144.f}) {
        char label[160];
        // Glyph at x = 4, centred on y = 12; Right: the fill covers the outline on the right side.
        // Sample the middle of the vertical outline: left side (outline only) and right side (outline + fill).
        const float x = 4, cy = 12, leftStroke = x + .75f, rightStroke = x + CM::Layout::glyphW - .75f;
        {   Canvas c(dpi, 28, 24); c.target->BeginDraw(); c.target->Clear(menu); PaintOld(c, CM::Glyph::Right, x, cy, .5f); c.target->EndDraw();
            const int l = c.At(leftStroke, cy), r = c.At(rightStroke, cy);
            std::snprintf(label, sizeof label, "%.0f dpi: the former drawing is brighter where the fill covers the outline (%d > %d), reproduced", dpi, r, l);
            Expect(r > l + 10, label);
            if (!out.empty()) c.Save(out + L"\\menu-glyph-before-" + std::to_wstring((int)dpi) + L".png"); }
        for (int highlighted = 0; highlighted < 2; ++highlighted) {
            Canvas c(dpi, 28, 24); c.target->BeginDraw(); c.target->Clear(highlighted ? row : menu);
            CM::PaintLayoutGlyph(c.target, c.brush, CM::Glyph::Right, x, cy, true); c.target->EndDraw();
            const int l = c.At(leftStroke, cy), r = c.At(rightStroke, cy), inside = c.At(x + CM::Layout::glyphW * .75f, cy);
            std::snprintf(label, sizeof label, "%.0f dpi%s: outline alone %d, outline under the fill %d, fill %d: one ink, no doubled strip", dpi, highlighted ? " highlighted row" : "", l, r, inside);
            Expect(std::abs(r - l) <= 2 && std::abs(inside - 0x8A) <= 1, label);
            if (!out.empty()) c.Save(out + L"\\menu-glyph-after-" + std::to_wstring((int)dpi) + (highlighted ? L"-highlighted" : L"") + L".png");
        }
        // Every glyph of the shared painter (split block and group rows): no pixel brighter than the
        // ink, antialiased corners included (a doubled overlap would exceed it).
        for (CM::Glyph g : {CM::Glyph::Right, CM::Glyph::Left, CM::Glyph::Top, CM::Glyph::Bottom, CM::Glyph::StartRow, CM::Glyph::EndRow, CM::Glyph::StartColumn, CM::Glyph::EndColumn}) {
            Canvas c(dpi, 28, 24); c.target->BeginDraw(); c.target->Clear(menu); CM::PaintLayoutGlyph(c.target, c.brush, g, x, cy, true); c.target->EndDraw();
            int peak = 0, inked = 0;
            for (UINT py = 0; py < c.h; ++py) for (UINT px = 0; px < c.w; ++px) { WICRect r{(INT)px, (INT)py, 1, 1}; BYTE p[4]{}; c.bitmap->CopyPixels(&r, 4, 4, p); peak = std::max<int>(peak, p[1]); if (p[1] >= 0x8A - 1) ++inked; }
            std::snprintf(label, sizeof label, "%.0f dpi glyph %d: brightest pixel %d (ink 138), %d pixels at full ink", dpi, (int)g, peak, inked);
            Expect(peak <= 0x8A + 1 && inked > 0, label);
        }
        {   Canvas c(dpi, 28, 24); c.target->BeginDraw(); c.target->Clear(menu);
            CM::PaintLayoutGlyph(c.target, c.brush, CM::Glyph::Bottom, x, cy, false); c.target->EndDraw();
            const int inside = c.At(x + CM::Layout::glyphW / 2, cy + CM::Layout::glyphH * .3f), top = c.At(x + CM::Layout::glyphW / 2, cy - CM::Layout::glyphH / 2 + .75f);
            std::snprintf(label, sizeof label, "%.0f dpi: disabled glyph keeps the former 32 %% level (fill %d, outline %d)", dpi, inside, top);
            Expect(std::abs(inside - 0x61) <= 1 && top <= 0x61 + 1, label); }
    }
    CoUninitialize();
    std::printf(failures ? "%d failure(s)\n" : "all menu glyph checks passed\n", failures);
    return failures ? 1 : 0;
}
