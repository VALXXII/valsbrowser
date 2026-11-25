#pragma once
// Print preview pages (#121, Paper « Proposition — Impression · A (avec aperçu) », page p-V-0).
// The PDF the engine produced for the current settings (ICoreWebView2_16::PrintToPdfStream, see
// src/print-integration.h) is rasterized one page at a time by Windows.Data.Pdf on a worker thread:
// a long document never blocks the UI thread, and only the pages the preview shows are drawn.
//
// No new link dependency (test hosts include main.cpp and link the UI libraries only): combase.dll
// (WinRT activation and strings) and shcore.dll (stream bridges) are loaded at run time from
// System32; WIC (windowscodecs) decodes the rendered page. The WinRT ABI interfaces are declared
// here from the Windows metadata (IIDs and method order checked against Windows.Data.winmd and
// Windows.Foundation.winmd); only the leading methods that are called are listed per interface.
//
//   Renderer r; r.Start(hwnd, WM_APP + n);         // results are announced by a posted message
//   r.Load(generation, pdfBytes);                  // replaces the document (older work is dropped)
//   r.Want(generation, page, widthPx, heightPx);   // one page, premultiplied BGRA
//   for (auto& res : r.Take()) ...                 // UI thread, after the message
//   r.Stop();
#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <inspectable.h>
#include <hstring.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace PrintPreview {
namespace Abi {
inline const IID IAsyncInfo{0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
inline const IID IRandomAccessStream{0x905A0FE1, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
inline const IID IPdfDocumentStatics{0x433A0B5F, 0xC007, 0x4788, {0x90, 0xF2, 0x08, 0x14, 0x3D, 0x92, 0x25, 0x99}};
inline const IID IPdfPageRenderOptions{0x3C98056F, 0xB7CF, 0x4C29, {0x9A, 0x04, 0x52, 0xD9, 0x02, 0x67, 0xF4, 0x25}};
inline const GUID BmpEncoder{0x69BE8BB4, 0xD66D, 0x47C8, {0x86, 0x5A, 0xED, 0x15, 0x89, 0x43, 0x37, 0x82}};   // BitmapEncoder.BmpEncoderId
enum Status { Started = 0, Completed = 1, Canceled = 2, Error = 3 };
struct Rect { float x, y, width, height; };
struct Color { BYTE a, r, g, b; };
struct AsyncInfo : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct AsyncOperation : IInspectable {   // IAsyncOperation<T*>: same layout for every T
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void**) = 0;
};
struct AsyncAction : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults() = 0;
};
struct PdfPageRenderOptions : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SourceRect(Rect*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SourceRect(Rect) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DestinationWidth(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DestinationWidth(UINT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DestinationHeight(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DestinationHeight(UINT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BackgroundColor(Color*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BackgroundColor(Color) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsIgnoringHighContrast(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsIgnoringHighContrast(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BitmapEncoderId(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BitmapEncoderId(GUID) = 0;
};
struct PdfPage : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RenderToStreamAsync(IUnknown*, AsyncAction**) = 0;
    virtual HRESULT STDMETHODCALLTYPE RenderWithOptionsToStreamAsync(IUnknown*, PdfPageRenderOptions*, AsyncAction**) = 0;
};
struct PdfDocument : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetPage(UINT32, PdfPage**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PageCount(UINT32*) = 0;
};
struct PdfDocumentStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE LoadFromFileAsync(IUnknown*, AsyncOperation**) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadFromFileWithPasswordAsync(IUnknown*, HSTRING, AsyncOperation**) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadFromStreamAsync(IUnknown*, AsyncOperation**) = 0;
};
inline const IID IActivationFactory{0x00000035, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
struct ActivationFactory : IInspectable { virtual HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) = 0; };
using GetFactoryFn = HRESULT(WINAPI*)(HSTRING, ActivationFactory**);   // Windows.Data.Pdf.dll!DllGetActivationFactory
// windows.data.pdf.interop.h (not in the toolchain): the native renderer draws a page with OUR Direct2D device.
struct PdfRenderParams { D2D1_RECT_F source; UINT32 width, height; D2D1_COLOR_F background; BOOLEAN ignoreHighContrast; };
struct PdfRendererNative : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE RenderPageToSurface(IUnknown* page, IUnknown* surface, POINT offset, PdfRenderParams* params) = 0;
    virtual HRESULT STDMETHODCALLTYPE RenderPageToDeviceContext(IUnknown* page, ID2D1DeviceContext* context, PdfRenderParams* params) = 0;
};
using CreateRendererFn = HRESULT(WINAPI*)(IUnknown* dxgiDevice, PdfRendererNative** renderer);   // Windows.Data.Pdf.dll!PdfCreateRenderer
using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
// combase / shcore entry points, resolved once from System32.
struct Api {
    HRESULT(WINAPI* getFactory)(HSTRING, REFIID, void**) = nullptr;
    HRESULT(WINAPI* activate)(HSTRING, IInspectable**) = nullptr;
    HRESULT(WINAPI* createString)(LPCWSTR, UINT32, HSTRING*) = nullptr;
    HRESULT(WINAPI* deleteString)(HSTRING) = nullptr;
    HRESULT(WINAPI* overStream)(IStream*, int, REFIID, void**) = nullptr;          // CreateRandomAccessStreamOverStream
    HRESULT(WINAPI* overRandomAccess)(IUnknown*, REFIID, void**) = nullptr;        // CreateStreamOverRandomAccessStream
    bool Ready() const { return getFactory && activate && createString && deleteString && overStream && overRandomAccess; }
    static const Api& Get() {
        static const Api api = [] {
            Api a;
            HMODULE combase = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto get = [](HMODULE m, const char* name) { return m ? reinterpret_cast<void*>(GetProcAddress(m, name)) : nullptr; };
            a.getFactory = reinterpret_cast<decltype(a.getFactory)>(get(combase, "RoGetActivationFactory"));
            a.activate = reinterpret_cast<decltype(a.activate)>(get(combase, "RoActivateInstance"));
            a.createString = reinterpret_cast<decltype(a.createString)>(get(combase, "WindowsCreateString"));
            a.deleteString = reinterpret_cast<decltype(a.deleteString)>(get(combase, "WindowsDeleteString"));
            a.overStream = reinterpret_cast<decltype(a.overStream)>(get(shcore, "CreateRandomAccessStreamOverStream"));
            a.overRandomAccess = reinterpret_cast<decltype(a.overRandomAccess)>(get(shcore, "CreateStreamOverRandomAccessStream"));
            return a;
        }();
        return api;
    }
};
template<class T> struct Ref {
    T* p = nullptr;
    Ref() = default; Ref(const Ref&) = delete; Ref& operator=(const Ref&) = delete;
    ~Ref() { if (p) p->Release(); }
    T* operator->() const { return p; }
    T** Out() { if (p) p->Release(); p = nullptr; return &p; }
    void** Void() { return reinterpret_cast<void**>(Out()); }
};
struct String {
    HSTRING h = nullptr;
    explicit String(const wchar_t* s) { const auto& api = Api::Get(); if (api.createString) api.createString(s, static_cast<UINT32>(wcslen(s)), &h); }
    ~String() { if (h) Api::Get().deleteString(h); }
    String(const String&) = delete; String& operator=(const String&) = delete;
};
}   // namespace Abi

// A finished piece of work, read on the UI thread.
struct Result {
    enum Kind { Count, Page, Failed } kind = Failed;
    unsigned generation = 0; int count = 0, page = -1; UINT width = 0, height = 0;
    std::vector<BYTE> pixels;   // premultiplied BGRA, stride width * 4
    HRESULT hr = S_OK;
};

class Renderer {
    struct Job { bool load = false; unsigned generation = 0; std::shared_ptr<const std::vector<BYTE>> pdf; int page = -1; UINT width = 0, height = 0; };
    std::thread thread_; std::mutex lock_; std::condition_variable wake_;
    std::deque<Job> jobs_; std::vector<Result> results_;
    std::atomic<unsigned> latest_{0}; std::atomic<bool> stop_{false};
    HWND notify_ = nullptr; UINT message_ = 0; bool started_ = false;

    bool Stale(unsigned generation) const { return stop_.load() || generation != latest_.load(); }
    void Post(Result r) {
        { std::lock_guard<std::mutex> g(lock_); results_.push_back(std::move(r)); }
        if (notify_) PostMessageW(notify_, message_, 0, 0);
    }
    // WinRT asynchronous work, waited for on this worker (MTA): the status is polled, and a stale
    // generation cancels it. Never touches the UI thread.
    HRESULT Wait(IInspectable* op, unsigned generation) const {
        Abi::Ref<Abi::AsyncInfo> info;
        if (!op || FAILED(op->QueryInterface(Abi::IAsyncInfo, info.Void())) || !info.p) return E_NOINTERFACE;
        int status = Abi::Started; HRESULT hr = S_OK;
        for (;;) {
            hr = info->get_Status(&status); if (FAILED(hr) || status != Abi::Started) break;
            if (Stale(generation)) {
                // Cancelled, but still waited for (up to 5 s): no work of Windows.Data.Pdf may outlive the worker,
                // which unloads the library when it ends.
                info->Cancel(); for (int i = 0; i < 2500 && SUCCEEDED(info->get_Status(&status)) && status == Abi::Started; ++i) Sleep(2);
                hr = E_ABORT; break;
            }
            Sleep(2);
        }
        if (SUCCEEDED(hr) && status == Abi::Error) { HRESULT e = E_FAIL; info->get_ErrorCode(&e); hr = FAILED(e) ? e : E_FAIL; }
        else if (SUCCEEDED(hr) && status == Abi::Canceled) hr = E_ABORT;
        return hr;   // never Close() here: GetResults() is still to be called (it would answer E_ILLEGAL_METHOD_CALL)
    }
    // Windows.Data.Pdf, loaded by the worker itself and activated through its own DllGetActivationFactory (not
    // combase's process-wide cache): the worker unloads it when it ends, while the process is alive. Left to the
    // process's exit, its teardown releases a D3D device after DXGI is gone and the process ends with 0x87A.
    HMODULE pdfModule_ = nullptr; Abi::GetFactoryFn pdfFactory_ = nullptr;
    Abi::Ref<Abi::PdfDocumentStatics> statics_; Abi::Ref<Abi::ActivationFactory> options_;
    HRESULT PdfClasses() {
        if (statics_.p && options_.p) return S_OK;
        if (!pdfModule_) pdfModule_ = LoadLibraryExW(L"Windows.Data.Pdf.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (pdfModule_ && !pdfFactory_) pdfFactory_ = reinterpret_cast<Abi::GetFactoryFn>(reinterpret_cast<void*>(GetProcAddress(pdfModule_, "DllGetActivationFactory")));
        if (!pdfFactory_) return E_NOTIMPL;
        Abi::Ref<Abi::ActivationFactory> doc; HRESULT hr;
        { Abi::String cls(L"Windows.Data.Pdf.PdfDocument"); hr = pdfFactory_(cls.h, doc.Out()); }
        if (SUCCEEDED(hr)) hr = doc->QueryInterface(Abi::IPdfDocumentStatics, statics_.Void());
        if (SUCCEEDED(hr)) { Abi::String cls(L"Windows.Data.Pdf.PdfPageRenderOptions"); hr = pdfFactory_(cls.h, options_.Out()); }
        return hr;
    }
    // Pages are drawn by the native renderer on a WARP (software) Direct3D device the worker creates and releases
    // itself: no GPU driver thread, and nothing of Windows.Data.Pdf's own device is left for the process's exit (its
    // stream renderer keeps one in a static that, released during the exit, ends the process with code 0x87A).
    Abi::Ref<ID3D11Device> d3d_; Abi::Ref<ID2D1Factory1> d2d_; Abi::Ref<ID2D1Device> d2dDevice_; Abi::Ref<ID2D1DeviceContext> dc_;
    Abi::Ref<Abi::PdfRendererNative> native_; bool nativeTried_ = false;
    HRESULT NativeRenderer() {
        if (native_.p) return S_OK;
        if (nativeTried_) return E_NOTIMPL;
        nativeTried_ = true;
        auto create = pdfModule_ ? reinterpret_cast<Abi::CreateRendererFn>(reinterpret_cast<void*>(GetProcAddress(pdfModule_, "PdfCreateRenderer"))) : nullptr;
        HMODULE d3dModule = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   // kept for the process: never unloaded under a live device
        auto device = d3dModule ? reinterpret_cast<Abi::CreateDeviceFn>(reinterpret_cast<void*>(GetProcAddress(d3dModule, "D3D11CreateDevice"))) : nullptr;
        if (!create || !device) return E_NOTIMPL;
        HRESULT hr = device(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, d3d_.Out(), nullptr, nullptr);
        Abi::Ref<IDXGIDevice> dxgi; if (SUCCEEDED(hr)) hr = d3d_->QueryInterface(__uuidof(IDXGIDevice), dxgi.Void());
        if (SUCCEEDED(hr)) { D2D1_FACTORY_OPTIONS o{}; hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &o, d2d_.Void()); }
        if (SUCCEEDED(hr)) hr = d2d_->CreateDevice(dxgi.p, d2dDevice_.Out());
        if (SUCCEEDED(hr)) hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.Out());
        if (SUCCEEDED(hr)) hr = create(dxgi.p, native_.Out());
        if (FAILED(hr)) ReleaseNative();
        return hr;
    }
    void ReleaseNative() { native_.Out(); dc_.Out(); d2dDevice_.Out(); d2d_.Out(); d3d_.Out(); }
    HRESULT RenderNative(Abi::PdfPage* page, const Job& job, Result& out) {
        HRESULT hr = NativeRenderer(); if (FAILED(hr)) return hr;
        const D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        const D2D1_BITMAP_PROPERTIES1 readProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        Abi::Ref<ID2D1Bitmap1> target, read;
        hr = dc_->CreateBitmap(D2D1::SizeU(job.width, job.height), nullptr, 0, &targetProps, target.Out());
        if (SUCCEEDED(hr)) hr = dc_->CreateBitmap(D2D1::SizeU(job.width, job.height), nullptr, 0, &readProps, read.Out());
        if (FAILED(hr)) return hr;
        dc_->SetTarget(target.p); dc_->BeginDraw(); dc_->Clear(D2D1::ColorF(1, 1, 1, 1));
        Abi::PdfRenderParams params{D2D1::RectF(0, 0, 0, 0), job.width, job.height, D2D1::ColorF(1, 1, 1, 1), TRUE};   // whole page; high contrast ignored: the sheet shows what prints
        hr = native_->RenderPageToDeviceContext(page, dc_.p, &params);
        const HRESULT end = dc_->EndDraw(); dc_->SetTarget(nullptr);
        if (SUCCEEDED(hr)) hr = end;
        if (SUCCEEDED(hr)) hr = read->CopyFromBitmap(nullptr, target.p, nullptr);
        D2D1_MAPPED_RECT map{}; if (SUCCEEDED(hr)) hr = read->Map(D2D1_MAP_OPTIONS_READ, &map);
        if (FAILED(hr)) { if (hr == D2DERR_RECREATE_TARGET) ReleaseNative(), nativeTried_ = false; return hr; }
        out.width = job.width; out.height = job.height; out.pixels.resize(static_cast<size_t>(job.width) * job.height * 4);
        for (UINT y = 0; y < job.height; ++y) memcpy(out.pixels.data() + static_cast<size_t>(y) * job.width * 4, map.bits + static_cast<size_t>(y) * map.pitch, static_cast<size_t>(job.width) * 4);
        read->Unmap();
        return S_OK;
    }
    void UnloadPdf() { ReleaseNative(); statics_.Out(); options_.Out(); }   // the module stays: unloading it here deadlocks with display drivers (its D3D device goes under the loader lock)
    HRESULT LoadDocument(const Job& job, Abi::Ref<Abi::PdfDocument>& doc, UINT32& count) {
        const auto& api = Abi::Api::Get(); if (!api.Ready()) return E_NOTIMPL;
        if (FAILED(PdfClasses())) return E_NOTIMPL;
        if (!job.pdf || job.pdf->empty() || job.pdf->size() > 0x7FFFFFFFu) return E_INVALIDARG;
        Abi::Ref<IStream> bytes; bytes.p = SHCreateMemStream(job.pdf->data(), static_cast<UINT>(job.pdf->size()));
        if (!bytes.p) return E_OUTOFMEMORY;
        Abi::Ref<IUnknown> stream; HRESULT hr = api.overStream(bytes.p, 0 /* BSOS_DEFAULT */, Abi::IRandomAccessStream, stream.Void()); if (FAILED(hr)) return hr;
        Abi::Ref<Abi::AsyncOperation> op; hr = statics_->LoadFromStreamAsync(stream.p, op.Out()); if (FAILED(hr)) return hr;
        hr = Wait(op.p, job.generation); if (FAILED(hr)) return hr;
        hr = op->GetResults(doc.Void()); if (FAILED(hr) || !doc.p) return FAILED(hr) ? hr : E_FAIL;
        return doc->get_PageCount(&count);
    }
    HRESULT RenderPage(IWICImagingFactory* wic, Abi::PdfDocument* doc, const Job& job, Result& out) {
        const auto& api = Abi::Api::Get();
        Abi::Ref<Abi::PdfPage> page; HRESULT hr = doc->GetPage(static_cast<UINT32>(job.page), page.Out()); if (FAILED(hr)) return hr;
        if (job.width > 16384 || job.height > 16384) return E_INVALIDARG;
        if (SUCCEEDED(RenderNative(page.p, job, out))) return S_OK;
        out.pixels.clear();   // no native renderer (older Windows): the stream renderer
        Abi::Ref<IInspectable> options; Abi::Ref<Abi::PdfPageRenderOptions> opt;
        hr = options_.p ? options_->ActivateInstance(options.Out()) : E_NOTIMPL;
        if (SUCCEEDED(hr)) hr = options->QueryInterface(Abi::IPdfPageRenderOptions, opt.Void());
        if (FAILED(hr)) return hr;
        opt->put_DestinationWidth(job.width); opt->put_DestinationHeight(job.height);
        opt->put_IsIgnoringHighContrast(TRUE);   // the sheet shows what prints, whatever the Windows contrast theme
        opt->put_BitmapEncoderId(Abi::BmpEncoder);   // uncompressed: the fastest round trip through the stream
        Abi::Ref<IInspectable> memory; Abi::Ref<IUnknown> target;
        { Abi::String cls(L"Windows.Storage.Streams.InMemoryRandomAccessStream"); hr = api.activate(cls.h, memory.Out()); }
        if (SUCCEEDED(hr)) hr = memory->QueryInterface(Abi::IRandomAccessStream, target.Void());
        if (FAILED(hr)) return hr;
        Abi::Ref<Abi::AsyncAction> action; hr = page->RenderWithOptionsToStreamAsync(target.p, opt.p, action.Out()); if (FAILED(hr)) return hr;
        hr = Wait(action.p, job.generation); if (FAILED(hr)) return hr;
        hr = action->GetResults(); if (FAILED(hr)) return hr;
        Abi::Ref<IStream> image; hr = api.overRandomAccess(target.p, IID_IStream, image.Void()); if (FAILED(hr)) return hr;
        LARGE_INTEGER zero{}; image->Seek(zero, STREAM_SEEK_SET, nullptr);
        Abi::Ref<IWICBitmapDecoder> decoder; Abi::Ref<IWICBitmapFrameDecode> frame; Abi::Ref<IWICFormatConverter> convert;
        hr = wic->CreateDecoderFromStream(image.p, nullptr, WICDecodeMetadataCacheOnLoad, decoder.Out());
        if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, frame.Out());
        if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(convert.Out());
        if (SUCCEEDED(hr)) hr = convert->Initialize(frame.p, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
        UINT w = 0, h = 0; if (SUCCEEDED(hr)) hr = convert->GetSize(&w, &h);
        if (SUCCEEDED(hr) && (!w || !h || w > 16384 || h > 16384)) hr = E_UNEXPECTED;
        if (FAILED(hr)) return hr;
        out.pixels.resize(static_cast<size_t>(w) * h * 4);
        hr = convert->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.pixels.size()), out.pixels.data());
        out.width = w; out.height = h;
        return hr;
    }
    void Run() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Abi::Ref<IWICImagingFactory> wic; CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, wic.Void());
        Abi::Ref<Abi::PdfDocument> doc; unsigned loaded = 0; UINT32 count = 0;
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> g(lock_);
                wake_.wait(g, [this] { return stop_.load() || !jobs_.empty(); });
                if (stop_.load()) break;
                job = std::move(jobs_.front()); jobs_.pop_front();
            }
            if (Stale(job.generation)) continue;
            if (job.load) {
                doc.Out(); loaded = 0; count = 0;
                const HRESULT hr = LoadDocument(job, doc, count);
                if (Stale(job.generation)) { doc.Out(); continue; }
                Result r; r.generation = job.generation; r.hr = hr;
                if (SUCCEEDED(hr) && doc.p) { loaded = job.generation; r.kind = Result::Count; r.count = static_cast<int>(count); }
                else { doc.Out(); r.kind = Result::Failed; }
                Post(std::move(r));
                continue;
            }
            if (!doc.p || loaded != job.generation || job.page < 0 || static_cast<UINT32>(job.page) >= count || !wic.p) continue;
            Result r; r.generation = job.generation; r.page = job.page; r.kind = Result::Page;
            r.hr = RenderPage(wic.p, doc.p, job, r);
            if (Stale(job.generation)) continue;
            if (FAILED(r.hr)) { r.kind = Result::Failed; r.pixels.clear(); }
            Post(std::move(r));
        }
        doc.Out(); wic.Out(); UnloadPdf();   // every PDF object is released: the library goes now, not at the process's exit
        if (SUCCEEDED(co)) CoUninitialize();
    }
public:
    ~Renderer() { Stop(); }
    // Windows.Data.Pdf and the stream bridges are present (Windows 10 and later).
    static bool Available() {
        static const bool present = [] {
            HMODULE m = LoadLibraryExW(L"Windows.Data.Pdf.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            const bool ok = m && GetProcAddress(m, "DllGetActivationFactory"); if (m) FreeLibrary(m); return ok;
        }();
        return present && Abi::Api::Get().Ready();
    }
    bool Start(HWND notify, UINT message) {
        if (started_) return true;
        notify_ = notify; message_ = message; stop_ = false;
        try { thread_ = std::thread([this] { Run(); }); } catch (...) { return false; }
        started_ = true; return true;
    }
    void Stop() {
        if (!started_) return;
        { std::lock_guard<std::mutex> g(lock_); stop_ = true; jobs_.clear(); }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
        started_ = false; std::lock_guard<std::mutex> g(lock_); results_.clear();
    }
    // A new document: everything queued or running for an older one is dropped.
    void Load(unsigned generation, std::shared_ptr<const std::vector<BYTE>> pdf) {
        { std::lock_guard<std::mutex> g(lock_); latest_ = generation; jobs_.clear(); Job j; j.load = true; j.generation = generation; j.pdf = std::move(pdf); jobs_.push_back(std::move(j)); }
        wake_.notify_all();
    }
    // Forget the current document (settings changed: a new one is coming).
    void Drop(unsigned generation) { { std::lock_guard<std::mutex> g(lock_); latest_ = generation; jobs_.clear(); } wake_.notify_all(); }
    // One page at `width` x `height` pixels. The newest wish goes first; a repeated wish moves up.
    void Want(unsigned generation, int page, UINT width, UINT height) {
        if (page < 0 || !width || !height) return;
        {
            std::lock_guard<std::mutex> g(lock_);
            if (generation != latest_.load()) return;
            jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& j) { return !j.load && j.page == page; }), jobs_.end());
            Job j; j.generation = generation; j.page = page; j.width = width; j.height = height;
            auto at = jobs_.begin(); while (at != jobs_.end() && at->load) ++at;   // after a pending load, before older wishes
            jobs_.insert(at, std::move(j));
        }
        wake_.notify_all();
    }
    std::vector<Result> Take() { std::lock_guard<std::mutex> g(lock_); std::vector<Result> out; out.swap(results_); return out; }
    unsigned Latest() const { return latest_.load(); }
};
}   // namespace PrintPreview
