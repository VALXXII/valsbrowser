#pragma once
// Annotation mode — capture processing (V2). Runs on a thread-pool thread, NEVER on the UI thread.
//
//   ProcessViewport  V3: the PNG of the whole viewport from Page.captureScreenshot WITHOUT a clip (a clipped shot
//                makes the engine re-render the view for the clip: the page's text vanished on screen for ~4
//                frames at every click, measured). Cropped here to the plan (element + margin), the frame's fill
//                removed, reduced to the store's limits, encoded. Only the rows down to the crop
//                are decoded.
//   ProcessClip  the PNG returned by Page.captureScreenshot (CDP, clip = element + margin, already cropped
//                and reduced by the engine): kept as is when it fits the store's limits (<= 1600 px,
//                <= 1.5 MiB; no re-encoding), otherwise reduced and re-encoded.
//   Process      fallback: the CapturePreview PNG of the whole viewport, cropped (element + margin, CSS px x
//                the measured capture/viewport ratio = DPR x zoom), reduced, encoded (WIC).
//   RemoveFill   V3: the page keeps its 8 % accent fill over the element during the shot (nothing may change on
//                screen); it is removed here: out = page * (1 - a) + accent * a  =>  page = (out - accent * a) / (1 - a),
//                a = opacity x the pixel's coverage by the fill box (exact at the box's fractional edges).
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
    std::string why;
    int cropX = 0, cropY = 0, cropW = 0, cropH = 0;   // V3: the crop in the viewport image (device px)
    const char* path = "";               // "cdp" | "preview"
};
// The page's fill to remove: box in image pixels (fractional), colour 0..255, opacity 0..1.
struct Fill { bool on = false; double x0 = 0, y0 = 0, x1 = 0, y1 = 0; double r = 0, g = 0, b = 0, alpha = 0; };
// Pure (tests): one channel of a pixel covered at `a` (= opacity x coverage) by `accent`.
inline int Unblend(int out, double accent, double a) {
    if (a <= 0 || a >= .999) return out;
    const double v = (out - accent * a) / (1 - a);
    return int(std::lround(std::clamp(v, 0.0, 255.0)));
}
inline double Coverage(double p0, double p1, double b0, double b1) { return std::max(0.0, std::min(p1, b1) - std::max(p0, b0)); }
// BGRA (or BGRX) rows in place.
inline void RemoveFill(std::uint8_t* bits, unsigned w, unsigned h, unsigned stride, const Fill& f) {
    if (!f.on || f.alpha <= 0) return;
    const int x0 = std::max(0, int(std::floor(f.x0))), x1 = std::min(int(w), int(std::ceil(f.x1)));
    const int y0 = std::max(0, int(std::floor(f.y0))), y1 = std::min(int(h), int(std::ceil(f.y1)));
    for (int y = y0; y < y1; ++y) {
        const double cy = Coverage(y, y + 1, f.y0, f.y1); if (cy <= 0) continue;
        std::uint8_t* row = bits + size_t(y) * stride;
        for (int x = x0; x < x1; ++x) {
            const double a = f.alpha * cy * Coverage(x, x + 1, f.x0, f.x1); if (a <= 0) continue;
            std::uint8_t* p = row + size_t(x) * 4;
            p[0] = std::uint8_t(Unblend(p[0], f.b, a)); p[1] = std::uint8_t(Unblend(p[1], f.g, a)); p[2] = std::uint8_t(Unblend(p[2], f.r, a));
        }
    }
}

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

// Fallback: capture = the CapturePreview PNG of the viewport; rect/viewport in CSS px.
inline Result Process(const std::string& capture, const BrowserAnnotations::Rect& rect, double viewW, double viewH) {
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
    r.ok = true; return r;
}

// The fill of the page's frame (V3) mapped into an image of w x h covering the clip; mark = CSS px of the viewport.
inline Fill FillIn(const AnnotationHost::ClipPlan& plan, const AnnotationHost::Mark* mark, unsigned w, unsigned h, double r, double g, double b) {
    Fill f; if (!mark || !mark->present || mark->fill <= 0 || plan.width <= 0 || plan.height <= 0) return f;
    // Device px: the engine draws the page at the scroll offset rounded to whole device pixels (a fractional scroll
    // moves the content by up to half a pixel against getBoundingClientRect), and the shot starts on a whole device
    // pixel. Measured: without this, the fill's first and last rows were off by up to 7 levels (QA, fill_probe.py).
    const double dpr = plan.dpr > 0 ? plan.dpr : 1, scale = plan.width > 0 ? (w / plan.width) / dpr : 1;
    auto axis = [&](double css0, double css1, double view, double scroll, double& o0, double& o1) {
        const double snap = scroll * dpr - std::round(scroll * dpr);           // content offset, device px
        const double origin = std::floor(view * dpr + snap + 1e-6);            // first device row of the shot
        o0 = (css0 * dpr + snap - origin) * scale; o1 = (css1 * dpr + snap - origin) * scale;
    };
    axis(mark->x, mark->x + mark->w, plan.viewX, plan.scrollX, f.x0, f.x1);
    axis(mark->y, mark->y + mark->h, plan.viewY, plan.scrollY, f.y0, f.y1);
    f.r = r; f.g = g; f.b = b; f.alpha = mark->fill; f.on = f.x1 > 0 && f.y1 > 0 && f.x0 < w && f.y0 < h;
    return f;
}
// V3: `png` = the whole viewport as on screen (device px). viewportCssW = the page's innerWidth (CSS px).
inline Result ProcessViewport(const std::string& png, const AnnotationHost::ClipPlan& plan, double viewportCssW,
                              const AnnotationHost::Mark* mark = nullptr, std::uint32_t accentRgb = 0) {
    using namespace detail; Result r; r.path = "cdp";
    Com com; Ptr<IWICImagingFactory> wic; if (!Factory(wic)) { r.why = "wic"; return r; }
    Ptr<IStream> in; in.p = SHCreateMemStream(reinterpret_cast<const BYTE*>(png.data()), UINT(png.size())); if (!in.p) { r.why = "stream"; return r; }
    Ptr<IWICBitmapDecoder> dec; Ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateDecoderFromStream(in.p, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) || FAILED(dec->GetFrame(0, &frame))) { r.why = "decode"; return r; }
    UINT iw = 0, ih = 0; frame->GetSize(&iw, &ih); if (!iw || !ih || !(viewportCssW > 0)) { r.why = "size"; return r; }
    const double k = double(iw) / viewportCssW;   // image px per CSS px (= devicePixelRatio)
    const int x0 = std::clamp(int(std::floor(plan.viewX * k + 1e-6)), 0, int(iw) - 1), y0 = std::clamp(int(std::floor(plan.viewY * k + 1e-6)), 0, int(ih) - 1);
    const int x1 = std::clamp(int(std::ceil((plan.viewX + plan.width) * k - 1e-6)), x0 + 1, int(iw)), y1 = std::clamp(int(std::ceil((plan.viewY + plan.height) * k - 1e-6)), y0 + 1, int(ih));
    const UINT w = UINT(x1 - x0), h = UINT(y1 - y0); r.cropX = x0; r.cropY = y0; r.cropW = int(w); r.cropH = int(h);
    Ptr<IWICBitmapClipper> clip; const WICRect cr{x0, y0, INT(w), INT(h)};
    Ptr<IWICFormatConverter> conv; Ptr<IWICBitmap> img;
    if (FAILED(wic->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(frame.p, &cr)) || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(clip.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(wic->CreateBitmapFromSource(conv.p, WICBitmapCacheOnLoad, &img))) { r.why = "crop"; return r; }
    if (mark && mark->present && mark->fill > 0) {   // the content sits at the scroll offset rounded to device px (see FillIn)
        Fill f; const double sx = plan.scrollX * k - std::round(plan.scrollX * k), sy = plan.scrollY * k - std::round(plan.scrollY * k);
        f.x0 = mark->x * k + sx - x0; f.x1 = (mark->x + mark->w) * k + sx - x0; f.y0 = mark->y * k + sy - y0; f.y1 = (mark->y + mark->h) * k + sy - y0;
        f.r = double((accentRgb >> 16) & 255); f.g = double((accentRgb >> 8) & 255); f.b = double(accentRgb & 255); f.alpha = mark->fill;
        f.on = f.x1 > 0 && f.y1 > 0 && f.x0 < w && f.y0 < h;
        if (f.on) {
            Ptr<IWICBitmapLock> lock; const WICRect all{0, 0, INT(w), INT(h)}; UINT stride = 0, size = 0; BYTE* bits = nullptr;
            if (FAILED(img->Lock(&all, WICBitmapLockWrite | WICBitmapLockRead, &lock)) || FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&size, &bits)) || !bits) { r.why = "fill: lock"; return r; }
            RemoveFill(bits, w, h, stride, f);
        }
    }
    const double scale = std::min(plan.scale > 0 ? plan.scale : 1.0, double(BrowserAnnotations::MaxShotSide) / std::max(w, h));
    if (!Fit(wic.p, img.p, w, h, scale, r)) return r;
    r.scale = r.width / double(w); r.crop = plan.crop;
    r.ok = true; return r;
}
// Engine clip: `png` is already the element + margin (AnnotationHost::PlanClip), devicePixelRatio = the page's.
// fill (V3): the page's frame fill to remove (FillIn), then the image is re-encoded.
inline Result ProcessClip(std::string png, const AnnotationHost::ClipPlan& plan, double devicePixelRatio,
                          const AnnotationHost::Mark* mark = nullptr, std::uint32_t accentRgb = 0) {
    using namespace detail; Result r; r.path = "cdp";
    Com com; Ptr<IWICImagingFactory> wic; if (!Factory(wic)) { r.why = "wic"; return r; }
    Ptr<IWICBitmap> img; UINT w = 0, h = 0;
    if (!Decode(wic.p, png, img, w, h)) { r.why = "decode"; return r; }
    const double device = std::max(1.0, plan.width * (devicePixelRatio > 0 ? devicePixelRatio : 1));
    const Fill fill = FillIn(plan, mark, w, h, double((accentRgb >> 16) & 255), double((accentRgb >> 8) & 255), double(accentRgb & 255));
    bool edited = false;
    if (fill.on) {   // to 32bpp BGRA in memory, then the fill out
        Ptr<IWICFormatConverter> conv; Ptr<IWICBitmap> bgra;
        if (FAILED(wic->CreateFormatConverter(&conv)) || FAILED(conv->Initialize(img.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
            FAILED(wic->CreateBitmapFromSource(conv.p, WICBitmapCacheOnLoad, &bgra))) { r.why = "fill: convert"; return r; }
        {   Ptr<IWICBitmapLock> lock; const WICRect all{0, 0, INT(w), INT(h)}; UINT stride = 0, size = 0; BYTE* bits = nullptr;
            if (FAILED(bgra->Lock(&all, WICBitmapLockWrite | WICBitmapLockRead, &lock)) || FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&size, &bits)) || !bits) { r.why = "fill: lock"; return r; }
            RemoveFill(bits, w, h, stride, fill);
        }
        img.p->Release(); img.p = bgra.p; bgra.p = nullptr; edited = true;
    }
    std::uint32_t pw = 0, ph = 0;
    if (!edited && BrowserAnnotations::InspectPng(png, pw, ph) && pw == w && ph == h) { r.png = std::move(png); r.width = w; r.height = h; r.scale = w / device; }
    else {   // too big (bytes or sides): reduce and re-encode
        const double k = std::min(1.0, double(BrowserAnnotations::MaxShotSide) / std::max(w, h));
        if (!Fit(wic.p, img.p, w, h, k, r)) return r;
        r.scale = r.width / device;
    }
    r.crop = plan.crop;
    r.ok = true; return r;
}

} // namespace AnnotationCapture
