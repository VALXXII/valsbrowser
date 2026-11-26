#pragma once
// Download rows: thumbnails of downloaded images (src/download-file-kind.h says which rows have one).
// A downloaded file is untrusted input, so the decode is careful:
//  - by content, never by name: the first bytes must be a PNG, JPEG, GIF, BMP, ICO, TIFF, WebP or HEIF/AVIF
//    signature, and that format's own Windows decoder is created by its class id (never a codec picked by
//    WIC's discovery, never an SVG: Windows has no SVG decoder and none is ever asked for);
//  - bounded: at most 64 MiB on disk (read once into memory, so what was checked is what is decoded) and
//    100 megapixels; a larger, empty, unreadable or corrupt file gets no thumbnail (the row shows the glyph);
//  - small: the centre square, scaled to the tile's pixels (cover), EXIF orientation applied;
//  - off the UI thread: one worker decodes in turn and posts `message` to the surface that asked; results
//    (failures too) are cached per file and pixel size, and re-checked against the file's size and write
//    time each time the panel opens.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace NativeChrome {
namespace Downloads {
namespace Thumbnails {
inline constexpr std::int64_t maxBytes = 64LL << 20;          // on disk
inline constexpr std::uint64_t maxPixels = 100000000ULL;       // decoded picture
inline constexpr int maxSide = 256;                            // thumbnail pixels (24 DIP at 400 % is 96)
inline constexpr size_t cacheLimit = 512;                      // entries kept (36 KB each at 96 px)

enum class Format { None, Png, Jpeg, Gif, Bmp, Ico, Tiff, Webp, Heif };
inline Format Sniff(const unsigned char* b, size_t n) {
    if (n >= 8 && std::memcmp(b, "\x89PNG\r\n\x1a\n", 8) == 0) return Format::Png;
    if (n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return Format::Jpeg;
    if (n >= 6 && (std::memcmp(b, "GIF87a", 6) == 0 || std::memcmp(b, "GIF89a", 6) == 0)) return Format::Gif;
    if (n >= 26 && b[0] == 'B' && b[1] == 'M') return Format::Bmp;
    if (n >= 6 && b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0 && (b[4] || b[5])) return Format::Ico;
    if (n >= 8 && (std::memcmp(b, "II*\0", 4) == 0 || std::memcmp(b, "MM\0*", 4) == 0)) return Format::Tiff;
    if (n >= 16 && std::memcmp(b, "RIFF", 4) == 0 && std::memcmp(b + 8, "WEBP", 4) == 0) return Format::Webp;
    if (n >= 12 && std::memcmp(b + 4, "ftyp", 4) == 0) {
        static const char* const brands[] = {"avif", "avis", "heic", "heix", "heim", "heis", "hevc", "hevx", "mif1", "msf1"};
        for (auto* brand : brands) if (std::memcmp(b + 8, brand, 4) == 0) return Format::Heif;
    }
    return Format::None;
}
// The Windows decoder of each format (wincodec.h's CLSID_WIC*Decoder, spelled out: no uuid library needed).
inline const CLSID* DecoderOf(Format format) {
    static const CLSID png = {0x389ea17b, 0x5078, 0x4cde, {0xb6, 0xef, 0x25, 0xc1, 0x51, 0x75, 0xc7, 0x51}};
    static const CLSID jpeg = {0x9456a480, 0xe88b, 0x43ea, {0x9e, 0x73, 0x0b, 0x2d, 0x9b, 0x71, 0xb1, 0xca}};
    static const CLSID gif = {0x381dda3c, 0x9ce9, 0x4834, {0xa2, 0x3e, 0x1f, 0x98, 0xf8, 0xfc, 0x52, 0xbe}};
    static const CLSID bmp = {0x6b462062, 0x7cbf, 0x400d, {0x9f, 0xdb, 0x81, 0x3d, 0xd1, 0x0f, 0x27, 0x78}};
    static const CLSID ico = {0xc61bfcdf, 0x2e0f, 0x4aad, {0xa8, 0xd7, 0xe0, 0x6b, 0xaf, 0xeb, 0xcd, 0xfe}};
    static const CLSID tiff = {0xb54e85d9, 0xfe23, 0x499f, {0x8b, 0x88, 0x6a, 0xce, 0xa7, 0x13, 0x75, 0x2b}};
    static const CLSID webp = {0x7693e886, 0x51c9, 0x4070, {0x84, 0x19, 0x9f, 0x70, 0x73, 0x8e, 0xc8, 0xfa}};
    static const CLSID heif = {0xe9a4a80a, 0x44fe, 0x4de4, {0x89, 0x71, 0x71, 0x50, 0xb1, 0x0a, 0x51, 0x99}};
    switch (format) {
    case Format::Png: return &png; case Format::Jpeg: return &jpeg; case Format::Gif: return &gif; case Format::Bmp: return &bmp;
    case Format::Ico: return &ico; case Format::Tiff: return &tiff; case Format::Webp: return &webp; case Format::Heif: return &heif;
    default: return nullptr;
    }
}
// EXIF orientation (1..8) → the flip/rotation that shows the picture upright. WIC flips first, then rotates
// (5, transpose: mirrored then turned 270; 7, transverse: mirrored then turned 90; tests/downloads checks all eight).
inline WICBitmapTransformOptions Upright(int orientation) {
    switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}

enum class Outcome { Ok, Missing, Empty, TooLarge, NotImage, NoDecoder, Corrupt };
struct Stamp {
    std::uint64_t size = 0, written = 0;
    bool operator==(const Stamp& o) const { return size == o.size && written == o.written; }
    bool operator!=(const Stamp& o) const { return !(*this == o); }
};
inline bool StampOf(const std::wstring& path, Stamp& stamp) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    stamp.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    stamp.written = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    return true;
}
// Premultiplied BGRA, side x side (D2D1 B8G8R8A8 premultiplied).
struct Pixels { int side = 0; std::vector<std::uint8_t> bgra; };
template<class T> struct Ref {
    T* p = nullptr; ~Ref() { if (p) p->Release(); }
    T** operator&() { return &p; } T* operator->() const { return p; } explicit operator bool() const { return p != nullptr; }
};
// Reads the whole file (the bytes checked are the bytes decoded). `wic`: a factory usable on this thread.
inline Outcome Decode(IWICImagingFactory* wic, const std::wstring& path, int side, Pixels& out, Format* format = nullptr) {
    out = {}; if (format) *format = Format::None;
    if (!wic || side <= 0 || side > maxSide) return Outcome::NoDecoder;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return Outcome::Missing;
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !GetFileSizeEx(file, &size)) { CloseHandle(file); return Outcome::Missing; }
    if (size.QuadPart <= 0) { CloseHandle(file); return Outcome::Empty; }
    if (size.QuadPart > maxBytes) { CloseHandle(file); return Outcome::TooLarge; }
    std::vector<std::uint8_t> bytes(static_cast<size_t>(size.QuadPart)); size_t got = 0;
    while (got < bytes.size()) {
        DWORD chunk = 0; const DWORD ask = static_cast<DWORD>(std::min<size_t>(bytes.size() - got, 1u << 24));
        if (!ReadFile(file, bytes.data() + got, ask, &chunk, nullptr) || chunk == 0) break;
        got += chunk;
    }
    CloseHandle(file);
    if (got != bytes.size()) return Outcome::Corrupt;
    const Format kind = Sniff(bytes.data(), bytes.size()); if (format) *format = kind;
    const CLSID* clsid = DecoderOf(kind); if (!clsid) return Outcome::NotImage;
    Ref<IWICStream> stream; Ref<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size())))) return Outcome::NoDecoder;
    if (FAILED(CoCreateInstance(*clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICBitmapDecoder), reinterpret_cast<void**>(&decoder))) || !decoder) return Outcome::NoDecoder;
    if (FAILED(decoder->Initialize(stream.p, WICDecodeMetadataCacheOnDemand))) return Outcome::Corrupt;
    UINT frames = 0; if (FAILED(decoder->GetFrameCount(&frames)) || frames == 0) return Outcome::Corrupt;
    // An icon file holds several sizes: its largest (first 32 frames).
    UINT pick = 0, width = 0, height = 0;
    for (UINT i = 0; i < (kind == Format::Ico ? std::min<UINT>(frames, 32) : 1u); ++i) {
        Ref<IWICBitmapFrameDecode> f; UINT w = 0, h = 0;
        if (SUCCEEDED(decoder->GetFrame(i, &f)) && SUCCEEDED(f->GetSize(&w, &h)) && static_cast<std::uint64_t>(w) * h > static_cast<std::uint64_t>(width) * height) { pick = i; width = w; height = h; }
    }
    if (!width || !height) return Outcome::Corrupt;
    if (static_cast<std::uint64_t>(width) * height > maxPixels) return Outcome::TooLarge;
    Ref<IWICBitmapFrameDecode> frame; if (FAILED(decoder->GetFrame(pick, &frame))) return Outcome::Corrupt;
    int orientation = 1;
    if (kind == Format::Jpeg || kind == Format::Tiff) {
        Ref<IWICMetadataQueryReader> query;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&query))) {
            PROPVARIANT value; PropVariantInit(&value);
            if (SUCCEEDED(query->GetMetadataByName(kind == Format::Jpeg ? L"/app1/ifd/{ushort=274}" : L"/ifd/{ushort=274}", &value)) && value.vt == VT_UI2) orientation = value.uiVal;
            PropVariantClear(&value);
        }
    }
    // Centre square, scaled to the tile, then upright.
    const UINT square = std::min(width, height);
    const WICRect crop{static_cast<INT>((width - square) / 2), static_cast<INT>((height - square) / 2), static_cast<INT>(square), static_cast<INT>(square)};
    Ref<IWICFormatConverter> converter; Ref<IWICBitmapClipper> clipper; Ref<IWICBitmapScaler> scaler; Ref<IWICBitmap> small; Ref<IWICBitmapFlipRotator> rotator;
    if (FAILED(wic->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return Outcome::Corrupt;
    if (FAILED(wic->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(converter.p, &crop))) return Outcome::Corrupt;
    if (FAILED(wic->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(clipper.p, static_cast<UINT>(side), static_cast<UINT>(side), WICBitmapInterpolationModeFant))) return Outcome::Corrupt;
    if (FAILED(wic->CreateBitmapFromSource(scaler.p, WICBitmapCacheOnLoad, &small))) return Outcome::Corrupt;
    IWICBitmapSource* last = small.p;
    if (Upright(orientation) != WICBitmapTransformRotate0) {
        if (FAILED(wic->CreateBitmapFlipRotator(&rotator)) || FAILED(rotator->Initialize(small.p, Upright(orientation)))) return Outcome::Corrupt;
        last = rotator.p;
    }
    Pixels pixels; pixels.side = side; pixels.bgra.resize(static_cast<size_t>(side) * side * 4);
    if (FAILED(last->CopyPixels(nullptr, static_cast<UINT>(side * 4), static_cast<UINT>(pixels.bgra.size()), pixels.bgra.data()))) return Outcome::Corrupt;
    out = std::move(pixels);
    return Outcome::Ok;
}

// A decoded (or failed) thumbnail. Immutable once published; `ok` false: show the glyph.
struct Thumb { bool ok = false; Outcome outcome = Outcome::Corrupt; Pixels pixels; Stamp stamp; };
using Key = std::pair<std::wstring, int>;   // file, pixel side

class Service {
    // waiters: every surface that asked while it was queued (the popover and the drawer may both ask).
    struct Entry { std::shared_ptr<const Thumb> thumb; bool queued = false; std::uint64_t used = 0; std::vector<std::pair<HWND, UINT>> waiters; };
    struct Job { Key key; bool recheck = false; };
    std::mutex lock_; std::condition_variable wake_, idle_;
    std::map<Key, Entry> cache_; std::deque<Job> jobs_;
    std::thread worker_; bool started_ = false; int busy_ = 0; std::uint64_t clock_ = 0;
    std::uint64_t decodes_ = 0;
    void Start() { if (started_) return; started_ = true; worker_ = std::thread([this] { Run(); }); worker_.detach(); }
    void Evict() {   // least recently used, never one being decoded
        while (cache_.size() > cacheLimit) {
            auto oldest = cache_.end();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) if (!it->second.queued && (oldest == cache_.end() || it->second.used < oldest->second.used)) oldest = it;
            if (oldest == cache_.end()) return;
            cache_.erase(oldest);
        }
    }
    void Run() {   // for the life of the process
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IWICImagingFactory* wic = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&wic));
        for (;;) {
            Job job;
            { std::unique_lock<std::mutex> hold(lock_); wake_.wait(hold, [this] { return !jobs_.empty(); }); job = jobs_.front(); jobs_.pop_front(); ++busy_; }
            Stamp now{}; const bool present = StampOf(job.key.first, now);
            std::shared_ptr<const Thumb> current;
            if (job.recheck) { std::lock_guard<std::mutex> hold(lock_); auto it = cache_.find(job.key); if (it != cache_.end()) current = it->second.thumb; }
            std::shared_ptr<Thumb> fresh;
            if (!job.recheck || !current || !present || current->stamp != now) {
                fresh = std::make_shared<Thumb>(); fresh->stamp = now;
                fresh->outcome = present ? Decode(wic, job.key.first, job.key.second, fresh->pixels) : Outcome::Missing;
                fresh->ok = fresh->outcome == Outcome::Ok;
            }
            std::vector<std::pair<HWND, UINT>> waiters;
            {
                std::lock_guard<std::mutex> hold(lock_);
                auto& entry = cache_[job.key]; entry.queued = false; waiters.swap(entry.waiters);
                if (fresh) { entry.thumb = fresh; entry.used = ++clock_; ++decodes_; }
                Evict(); --busy_;
            }
            if (fresh) for (auto& w : waiters) PostMessageW(w.first, w.second, 0, 0);
            idle_.notify_all();
        }
    }
    void Queue(const Key& key, bool recheck, HWND notify, UINT message) {   // lock held
        auto& entry = cache_[key]; entry.used = ++clock_;
        if (notify && std::find(entry.waiters.begin(), entry.waiters.end(), std::make_pair(notify, message)) == entry.waiters.end()) entry.waiters.emplace_back(notify, message);
        if (entry.queued) return;
        entry.queued = true; jobs_.push_back({key, recheck}); Start(); wake_.notify_one();
    }
public:
    // One per process, never destroyed: its worker may still wait when the process ends.
    static Service& Get() { static Service* service = new Service; return *service; }
    // UI thread, no disk access: the thumbnail (ok or failed) once decoded; null while it is being decoded
    // (queued now if new; `notify` then receives `message` when it is ready).
    std::shared_ptr<const Thumb> Lookup(const std::wstring& path, int side, HWND notify, UINT message) {
        if (path.empty() || side <= 0 || side > maxSide) return nullptr;
        std::lock_guard<std::mutex> hold(lock_);
        const Key key{path, side}; auto it = cache_.find(key);
        if (it != cache_.end() && it->second.thumb) { it->second.used = ++clock_; return it->second.thumb; }
        Queue(key, false, notify, message); Evict();
        return nullptr;
    }
    // The panel opens: every cached thumbnail of `path` is checked against the file on the worker (changed:
    // decoded again, then `message`).
    void Recheck(const std::wstring& path, HWND notify, UINT message) {
        std::lock_guard<std::mutex> hold(lock_);
        std::vector<Key> keys; for (auto& entry : cache_) if (entry.first.first == path && entry.second.thumb) keys.push_back(entry.first);
        for (auto& key : keys) Queue(key, true, notify, message);
    }
    // Tests.
    bool WaitIdleForTest(DWORD ms) {
        std::unique_lock<std::mutex> hold(lock_);
        return idle_.wait_for(hold, std::chrono::milliseconds(ms), [this] { return jobs_.empty() && busy_ == 0; });
    }
    std::uint64_t DecodesForTest() { std::lock_guard<std::mutex> hold(lock_); return decodes_; }
    bool KnownForTest(const std::wstring& path) { std::lock_guard<std::mutex> hold(lock_); for (auto& entry : cache_) if (entry.first.first == path) return true; return false; }
};
} // namespace Thumbnails
} // namespace Downloads
} // namespace NativeChrome
