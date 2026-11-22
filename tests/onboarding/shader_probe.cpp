// Onboarding shaders, alone: renders the three Paper shader nodes at their Paper size through
// src/onboarding-shaders.h and saves them as PNG (each over its node's CSS background), for the
// side-by-side comparison with the Paper screenshots (tests/onboarding/compare.py).
// Usage: shader_probe.exe <repo root> <output dir> [--software]
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <string>
#include "../../src/onboarding-shaders.h"

namespace S = Onboarding::Shaders;
static IWICImagingFactory* wic = nullptr;

static bool SavePng(const std::wstring& path, const S::Frame& f, unsigned background) {
    std::vector<std::uint8_t> px(f.bgra.size());
    const int bg[3] = {static_cast<int>(background & 255), static_cast<int>((background >> 8) & 255), static_cast<int>((background >> 16) & 255)};   // B G R
    for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
        const int a = f.bgra[i + 3];
        for (int c = 0; c < 3; ++c) px[i + c] = static_cast<std::uint8_t>(std::min(255, f.bgra[i + c] + bg[c] * (255 - a) / 255));
        px[i + 3] = 255;
    }
    IWICStream* stream = nullptr; IWICBitmapEncoder* encoder = nullptr; IWICBitmapFrameEncode* frame = nullptr; bool ok = false;
    if (SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(f.width, f.height))) {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WritePixels(f.height, f.width * 4, static_cast<UINT>(px.size()), px.data())) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    }
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring root = argc > 1 ? argv[1] : L".", out = argc > 2 ? argv[2] : L".";
    const bool software = argc > 3 && std::wstring(argv[3]) == L"--software";
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    S::Renderer r;
    if (!r.Init(software)) { std::printf("FAIL init: %s\n", r.Error().c_str()); return 1; }
    std::printf("device: %s\n", r.Animated() ? "hardware" : "software");
    const std::wstring assets = root + L"\\assets\\onboarding\\";
    S::Image logo, gem, noise;
    if (!S::LoadPng(wic, assets + L"halftone-logo.png", logo) || !S::LoadPng(wic, assets + L"gem-shape.png", gem, true) || !S::LoadPng(wic, assets + L"noise.png", noise)) { std::printf("FAIL assets\n"); return 1; }
    r.SetInput(S::Kind::Halftone, logo); r.SetInput(S::Kind::GemSmoke, gem); r.SetInput(S::Kind::GrainGradient, noise);
    int failures = 0; S::Frame f;
    auto shot = [&](const wchar_t* name, S::Kind kind, const S::Uniforms& u, float w, float h, float scale, unsigned bg) {
        LARGE_INTEGER a, b, q; QueryPerformanceFrequency(&q); QueryPerformanceCounter(&a);
        const bool ok = r.Render(kind, u, w, h, static_cast<unsigned>(std::lround(w * scale)), static_cast<unsigned>(std::lround(h * scale)), f);
        QueryPerformanceCounter(&b);
        const bool saved = ok && SavePng(out + L"\\" + name, f, bg);
        std::printf("%s %ls %ux%u %.2f ms\n", saved ? "ok  " : "FAIL", name, f.width, f.height, (b.QuadPart - a.QuadPart) * 1000.0 / q.QuadPart);
        if (!saved) ++failures;
    };
    shot(L"halftone-721x579@1.png", S::Kind::Halftone, S::Halftone(), 721, 579, 1, 0x171717);
    shot(L"halftone-721x579@2.png", S::Kind::Halftone, S::Halftone(), 721, 579, 2, 0x171717);
    shot(L"halftone-721x508@1.png", S::Kind::Halftone, S::Halftone(), 721, 508, 1, 0x171717);
    shot(L"halftone-721x508@1.25.png", S::Kind::Halftone, S::Halftone(), 721, 508, 1.25f, 0x171717);
    shot(L"halftone-721x579@2-drift25.png", S::Kind::Halftone, S::Halftone(.25f), 721, 579, 2, 0x171717);
    shot(L"halftone-721x579@2-drift50.png", S::Kind::Halftone, S::Halftone(.5f), 721, 579, 2, 0x171717);
    shot(L"halftone-721x579@2-drift100.png", S::Kind::Halftone, S::Halftone(1.f), 721, 579, 2, 0x171717);
    shot(L"gem-58x57@2.png", S::Kind::GemSmoke, S::GemSmoke(S::GemStillSeconds), 58, 57, 2, 0x171717);
    shot(L"gem-58x57@8.png", S::Kind::GemSmoke, S::GemSmoke(S::GemStillSeconds), 58, 57, 8, 0x171717);
    shot(L"grain-725x407@1.png", S::Kind::GrainGradient, S::GrainGradient(S::GrainStillSeconds), 725, 407, 1, 0xDEFDFF);
    shot(L"grain-725x407@1-plus8s.png", S::Kind::GrainGradient, S::GrainGradient(S::GrainStillSeconds + 8), 725, 407, 1, 0xDEFDFF);
    if (argc > 3 && std::wstring(argv[3]) == L"--scan") {   // find the time of a Paper screenshot (animated nodes)
        for (int i = 0; i < 315; ++i) { wchar_t name[64]; swprintf(name, 64, L"grain-%03d.png", i); r.Render(S::Kind::GrainGradient, S::GrainGradient(i * .1f), 725, 407, 725, 407, f); SavePng(out + L"\\" + name, f, 0xDEFDFF); }
        for (int i = 0; i < 126; ++i) { wchar_t name[64]; swprintf(name, 64, L"gem-%03d.png", i); r.Render(S::Kind::GemSmoke, S::GemSmoke(i * .05f), 58, 57, 116, 114, f); SavePng(out + L"\\" + name, f, 0x171717); }
        return 0;
    }
    for (int i = 0; i < 20; ++i) r.Render(S::Kind::Halftone, S::Halftone(i / 20.f), 721, 508, 721, 508, f);
    LARGE_INTEGER a, b, q; QueryPerformanceFrequency(&q); QueryPerformanceCounter(&a);
    for (int i = 0; i < 60; ++i) r.Render(S::Kind::Halftone, S::Halftone(i / 60.f), 721, 508, 721, 508, f);
    QueryPerformanceCounter(&b);
    std::printf("halftone 721x508@1: %.2f ms per frame (render + read back)\n", (b.QuadPart - a.QuadPart) * 1000.0 / q.QuadPart / 60);
    return failures ? 1 : 0;
}
