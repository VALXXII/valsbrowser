#pragma once
// Annotation mode — capture processing (V2). Runs on a thread-pool thread, NEVER on the UI thread.
//
//   ProcessClip  the PNG returned by Page.captureScreenshot (CDP, clip = element + margin, already cropped
//                and reduced by the engine): kept as is when it fits the store's limits (<= 1600 px,
//                <= 1.5 MiB; no re-encoding), otherwise reduced and re-encoded; + the popover thumbnail.
//   Process      fallback: the CapturePreview PNG of the whole viewport, cropped (element + margin, CSS px x
//                the measured capture/viewport ratio = DPR x zoom), reduced, encoded (WIC) + thumbnail.
//   Thumbnail    history rows and detail: a stored shot.png scaled to cover a box (premultiplied BGRA).
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
    double scale = 1;                    // png pixels per device pixel of the page
    std::string crop;
    unsigned thumbW = 0, thumbH = 0; std::vector<std::uint32_t> thumb;   // premultiplied BGRA
    float markL = 0, markT = 0, markR = 0, markB = 0;                     // element inside the thumbnail
    std::string why;
    const char* path = "";               // "cdp" | "preview"
};
struct Image { unsigned width = 0, height = 0; std::vector<std::uint32_t> pixels; };   // premultiplied BGRA

namespace detail {
template<class T> struct Ptr { T* p = nullptr; ~Ptr() { if (p) p->Release(); } T** operator&() { return &p; } T* operator->() const { return p; } };
struct Com { HRESULT hr; Com() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {} ~Com() { if (SUCCEEDED(hr)) CoUninitialize(); } };
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
inline bool Factory(Ptr<IWICImagingFactory>& wic) { return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic.p))); }
inline bool Decode(IWICImagingFactory* wic, const std::string& png, Ptr<IWICBitmap>& out, UINT& w, UINT& h) {
    Ptr<IStream> in; in.p = SHCreateMemStream(reinterpret_cast<const BYTE*>(png.data()), UINT(png.size()));
    if (!in.p) return false;
    Ptr<IWICBitmapDecoder> dec; Ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateDecoderFromStream(in.p, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) || FAILED(dec->GetFrame(0, &frame))) return false;
    // Decode once to memory: the clipper/scaler chain then never re-reads the PNG.
    if (FAILED(wic->CreateBitmapFromSource(frame.p, WICBitmapCacheOnLoad, &out))) return false;
    out->GetSize(&w, &h); return w && h;
}
// Scales `src` (w x h) by k into premultiplied BGRA.
inline bool Pixels(IWICImagingFactory* wic, IWICBitmapSource* src, UINT tw, UINT th, std::vector<std::uint32_t>& px) {
    Ptr<IWICBitmapScaler> ts; Ptr<IWICFormatConverter> tc;
    if (FAILED(wic->CreateBitmapScaler(&ts)) || FAILED(ts->Initialize(src, tw, th, WICBitmapInterpolationModeFant)) ||
        FAILED(wic->CreateFormatConverter(&tc)) || FAILED(tc->Initialize(ts.p, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    px.resize(size_t(tw) * th);
    if (FAILED(tc->CopyPixels(nullptr, tw * 4, UINT(px.size() * 4), reinterpret_cast<BYTE*>(px.data())))) { px.clear(); return false; }
    return true;
}
// The popover thumbnail: the crop scaled to cover thumbMax (the popover crops), mark = element in it.
inline void Thumb(IWICImagingFactory* wic, IWICBitmapSource* src, UINT w, UINT h, double mx, double my, double mw, double mh, unsigned thumbMaxW, unsigned thumbMaxH, Result& r) {
    const double k = std::min(1.0, std::max(double(thumbMaxW) / w, double(thumbMaxH) / h));
    const UINT tw = std::max(1u, UINT(std::lround(w * k))), th = std::max(1u, UINT(std::lround(h * k)));
    if (Pixels(wic, src, tw, th, r.thumb)) {
        r.thumbW = tw; r.thumbH = th;
        r.markL = float(mx * k); r.markT = float(my * k); r.markR = float((mx + mw) * k); r.markB = float((my + mh) * k);
    }
}
// Reduces `src` until the PNG fits MaxShotBytes (and maxSide), starting at `scale`.
inline bool Fit(IWICImagingFactory* wic, IWICBitmapSource* src, UINT w, UINT h, double scale, Result& r) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const UINT ow = std::max(1u, UINT(std::lround(w * scale))), oh = std::max(1u, UINT(std::lround(h * scale)));
        std::string png;
        if (ow == w && oh == h) { if (!Encode(wic, src, png)) { r.why = "encode"; return false; } }
        else {
            Ptr<IWICBitmapScaler> sc;
            if (FAILED(wic->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(src, ow, oh, WICBitmapInterpolationModeFant)) || !Encode(wic, sc.p, png)) { r.why = "scale"; return false; }
        }
        if (png.size() <= BrowserAnnotations::MaxShotBytes) { r.png = std::move(png); r.width = ow; r.height = oh; r.scale = scale; return true; }
        scale *= .75;
    }
    r.why = "too large"; return false;
}
}

// Fallback: capture = the CapturePreview PNG of the viewport; rect/viewport in CSS px; thumbMaxW/H in pixels.
inline Result Process(const std::string& capture, const BrowserAnnotations::Rect& rect, double viewW, double viewH, unsigned thumbMaxW, unsigned thumbMaxH) {
    using namespace detail; Result r; r.path = "preview";
    Com com; Ptr<IWICImagingFactory> wic; if (!Factory(wic)) { r.why = "wic"; return r; }
    Ptr<IWICBitmap> full; UINT cw = 0, ch = 0;
    if (!Decode(wic.p, capture, full, cw, ch)) { r.why = "decode"; return r; }
    auto plan = AnnotationHost::PlanCrop(rect, viewW, viewH, cw, ch);
    if (!plan.ok) { r.why = "outside the viewport"; return r; }
    Ptr<IWICBitmapClipper> clip; const WICRect cr{plan.x, plan.y, plan.w, plan.h};
    if (FAILED(wic->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(full.p, &cr))) { r.why = "clip"; return r; }
    if (!Fit(wic.p, clip.p, UINT(plan.w), UINT(plan.h), plan.scale, r)) return r;
    r.crop = plan.crop;
    Thumb(wic.p, clip.p, UINT(plan.w), UINT(plan.h), plan.markX, plan.markY, plan.markW, plan.markH, thumbMaxW, thumbMaxH, r);
    r.ok = true; return r;
}

// Engine clip: `png` is already the element + margin (AnnotationHost::PlanClip), devicePixelRatio = the page's.
inline Result ProcessClip(std::string png, const AnnotationHost::ClipPlan& plan, double devicePixelRatio, unsigned thumbMaxW, unsigned thumbMaxH) {
    using namespace detail; Result r; r.path = "cdp";
    Com com; Ptr<IWICImagingFactory> wic; if (!Factory(wic)) { r.why = "wic"; return r; }
    Ptr<IWICBitmap> img; UINT w = 0, h = 0;
    if (!Decode(wic.p, png, img, w, h)) { r.why = "decode"; return r; }
    const double device = std::max(1.0, plan.width * (devicePixelRatio > 0 ? devicePixelRatio : 1));
    std::uint32_t pw = 0, ph = 0;
    if (BrowserAnnotations::InspectPng(png, pw, ph) && pw == w && ph == h) { r.png = std::move(png); r.width = w; r.height = h; r.scale = w / device; }
    else {   // too big (bytes or sides): reduce and re-encode
        const double k = std::min(1.0, double(BrowserAnnotations::MaxShotSide) / std::max(w, h));
        if (!Fit(wic.p, img.p, w, h, k, r)) return r;
        r.scale = r.width / device;
    }
    r.crop = plan.crop;
    const double px = double(w) / plan.width;   // image pixels per CSS px of the clip
    Thumb(wic.p, img.p, w, h, plan.markX * px, plan.markY * px, plan.markW * px, plan.markH * px, thumbMaxW, thumbMaxH, r);
    r.ok = true; return r;
}

// A stored shot.png scaled to cover boxW x boxH pixels (centre crop), premultiplied BGRA.
inline bool Thumbnail(const std::string& png, unsigned boxW, unsigned boxH, Image& out) {
    using namespace detail;
    Com com; Ptr<IWICImagingFactory> wic; if (!Factory(wic) || !boxW || !boxH) return false;
    Ptr<IWICBitmap> img; UINT w = 0, h = 0;
    if (!Decode(wic.p, png, img, w, h)) return false;
    const double k = std::max(double(boxW) / w, double(boxH) / h);
    const UINT sw = std::max(boxW, UINT(std::lround(w * k))), sh = std::max(boxH, UINT(std::lround(h * k)));
    std::vector<std::uint32_t> px; if (!Pixels(wic.p, img.p, sw, sh, px)) return false;
    out.width = boxW; out.height = boxH; out.pixels.resize(size_t(boxW) * boxH);
    const UINT ox = (sw - boxW) / 2, oy = (sh - boxH) / 2;
    for (UINT y = 0; y < boxH; ++y) std::copy_n(px.begin() + (size_t(y + oy) * sw + ox), boxW, out.pixels.begin() + size_t(y) * boxW);
    return true;
}
} // namespace AnnotationCapture
