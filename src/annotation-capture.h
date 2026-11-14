#pragma once
// Annotation mode — capture processing (V1). Runs on a thread-pool thread, NEVER on the UI
// thread: decodes the CapturePreview PNG of the viewport, crops the element + margin (CSS px
// x the measured capture/viewport ratio, i.e. DPR x zoom), reduces it to <= 1600 px and
// <= 1.5 MiB, encodes PNG (WIC), and makes the popover thumbnail (premultiplied BGRA).
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "annotation-host.h"

namespace AnnotationCapture {
struct Result {
    bool ok = false;
    std::string png;                     // cropped, reduced, encoded
    unsigned width = 0, height = 0;      // of `png`
    double scale = 1;                    // png pixels per capture pixel
    std::string crop;
    unsigned thumbW = 0, thumbH = 0; std::vector<std::uint32_t> thumb;   // premultiplied BGRA
    float markL = 0, markT = 0, markR = 0, markB = 0;                     // element inside the thumbnail
    std::string why;
};

namespace detail {
template<class T> struct Ptr { T* p = nullptr; ~Ptr() { if (p) p->Release(); } T** operator&() { return &p; } T* operator->() const { return p; } };
inline bool Encode(IWICImagingFactory* wic, IWICBitmapSource* src, std::string& out) {
    Ptr<IStream> stream; if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;
    Ptr<IWICBitmapEncoder> enc; if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(stream.p, WICBitmapEncoderNoCache))) return false;
    Ptr<IWICBitmapFrameEncode> frame; if (FAILED(enc->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr))) return false;
    UINT w = 0, h = 0; src->GetSize(&w, &h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetSize(w, h)) || FAILED(frame->SetPixelFormat(&fmt))) return false;
    Ptr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv)) || FAILED(conv->Initialize(src, fmt, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    if (FAILED(frame->WriteSource(conv.p, nullptr)) || FAILED(frame->Commit()) || FAILED(enc->Commit())) return false;
    HGLOBAL mem = nullptr; if (FAILED(GetHGlobalFromStream(stream.p, &mem))) return false;
    STATSTG st{}; if (FAILED(stream->Stat(&st, STATFLAG_NONAME))) return false;
    const void* data = GlobalLock(mem); if (!data) return false;
    out.assign(static_cast<const char*>(data), size_t(st.cbSize.QuadPart)); GlobalUnlock(mem);
    return true;
}
}

// capture: the PNG bytes of CapturePreview; rect/viewport in CSS px; thumbMaxW/H in pixels.
inline Result Process(const std::string& capture, const BrowserAnnotations::Rect& rect, double viewW, double viewH, unsigned thumbMaxW, unsigned thumbMaxH) {
    using namespace detail; Result r;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Uninit { HRESULT hr; ~Uninit() { if (SUCCEEDED(hr)) CoUninitialize(); } } uninit{co};
    Ptr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic.p)))) { r.why = "wic"; return r; }
    Ptr<IStream> in; in.p = SHCreateMemStream(reinterpret_cast<const BYTE*>(capture.data()), UINT(capture.size()));
    if (!in.p) { r.why = "stream"; return r; }
    Ptr<IWICBitmapDecoder> dec; Ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateDecoderFromStream(in.p, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) || FAILED(dec->GetFrame(0, &frame))) { r.why = "decode"; return r; }
    UINT cw = 0, ch = 0; frame->GetSize(&cw, &ch);
    auto plan = AnnotationHost::PlanCrop(rect, viewW, viewH, cw, ch);
    if (!plan.ok) { r.why = "outside the viewport"; return r; }
    // Decode once to memory: the clipper/scaler chain then never re-reads the PNG.
    Ptr<IWICBitmap> full; if (FAILED(wic->CreateBitmapFromSource(frame.p, WICBitmapCacheOnLoad, &full))) { r.why = "bitmap"; return r; }
    Ptr<IWICBitmapClipper> clip; const WICRect cr{plan.x, plan.y, plan.w, plan.h};
    if (FAILED(wic->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(full.p, &cr))) { r.why = "clip"; return r; }
    double scale = plan.scale;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const UINT ow = std::max(1u, UINT(std::lround(plan.w * scale))), oh = std::max(1u, UINT(std::lround(plan.h * scale)));
        std::string png;
        if (ow == UINT(plan.w) && oh == UINT(plan.h)) { if (!Encode(wic.p, clip.p, png)) { r.why = "encode"; return r; } }
        else {
            Ptr<IWICBitmapScaler> sc;
            if (FAILED(wic->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(clip.p, ow, oh, WICBitmapInterpolationModeFant)) || !Encode(wic.p, sc.p, png)) { r.why = "scale"; return r; }
        }
        if (png.size() <= BrowserAnnotations::MaxShotBytes) { r.png = std::move(png); r.width = ow; r.height = oh; r.scale = scale; break; }
        scale *= .75;
    }
    if (r.png.empty()) { r.why = "too large"; return r; }
    r.crop = plan.crop;
    // Thumbnail: the crop fitted in thumbMax (cover is done by the popover), premultiplied BGRA.
    const double k = std::min(1.0, std::max(double(thumbMaxW) / plan.w, double(thumbMaxH) / plan.h));   // covers the box
    const UINT tw = std::max(1u, UINT(std::lround(plan.w * k))), th = std::max(1u, UINT(std::lround(plan.h * k)));
    Ptr<IWICBitmapScaler> ts; Ptr<IWICFormatConverter> tc;
    if (SUCCEEDED(wic->CreateBitmapScaler(&ts)) && SUCCEEDED(ts->Initialize(clip.p, tw, th, WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(wic->CreateFormatConverter(&tc)) && SUCCEEDED(tc->Initialize(ts.p, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        r.thumb.resize(size_t(tw) * th);
        if (SUCCEEDED(tc->CopyPixels(nullptr, tw * 4, UINT(r.thumb.size() * 4), reinterpret_cast<BYTE*>(r.thumb.data())))) {
            r.thumbW = tw; r.thumbH = th;
            r.markL = float(plan.markX * k); r.markT = float(plan.markY * k); r.markR = float((plan.markX + plan.markW) * k); r.markB = float((plan.markY + plan.markH) * k);
        } else r.thumb.clear();
    }
    r.ok = true; return r;
}
} // namespace AnnotationCapture
