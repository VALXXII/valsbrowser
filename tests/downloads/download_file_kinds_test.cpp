// Download rows' file icons (src/download-file-kind.h, src/download-thumbnails.h): the type by extension and MIME
// type, the glyph markup, the thumbnail decode and its fallbacks (missing, empty, huge, corrupt, a pixel bomb, an
// SVG under any name: never decoded), the cache and its re-check, then the popover drawn off screen (no window)
// with a png, a jpg being downloaded, an svg and a zip.
// Usage: download_file_kinds_test.exe <repo root> <output folder (throwaway: test files are written there)>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "../../src/download-panel.h"

using namespace NativeChrome;
using namespace NativeChrome::Downloads;
namespace Th = NativeChrome::Downloads::Thumbnails;
static int failures = 0;
static void Expect(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

static void KindChecks() {
    for (auto* name : {L"C:\\d\\photo.png", L"C:\\d\\PHOTO.JPG", L"C:\\d\\a.jpeg", L"C:\\d\\a.gif", L"C:\\d\\a.webp", L"C:\\d\\a.avif", L"C:\\d\\a.bmp", L"C:\\d\\a.ico", L"C:\\d\\a.tiff", L"C:\\d\\a.heic", L"C:\\d\\a.jfif"})
        Expect(KindOf(name, L"") == FileKind::Image, "kind: an image extension is an image (any case)");
    Expect(KindOf(L"C:\\d\\logo.svg", L"") == FileKind::Vector && KindOf(L"C:\\d\\logo.SVGZ", L"image/svg+xml") == FileKind::Vector, "kind: .svg / .svgz are vectors");
    Expect(KindOf(L"C:\\d\\logo.png", L"image/svg+xml") == FileKind::Vector, "kind: an SVG MIME type keeps an image-named file from being decoded");
    Expect(KindOf(L"C:\\d\\setup.exe", L"image/png") == FileKind::Other && KindOf(L"C:\\d\\a.zip", L"image/svg+xml") == FileKind::Other, "kind: the MIME type never turns another extension into an image or a vector");
    Expect(KindOf(L"C:\\d\\download", L"image/jpeg") == FileKind::Image && KindOf(L"C:\\d\\download", L" Image/SVG+XML; charset=utf-8") == FileKind::Vector, "kind: without an extension the MIME type decides");
    Expect(KindOf(L"C:\\d\\download", L"application/octet-stream") == FileKind::Other && KindOf(L"C:\\d\\download", L"") == FileKind::Other && KindOf(L"C:\\d\\download", L"image/") == FileKind::Other, "kind: no extension, no image MIME: the shell icon");
    Expect(KindOf(L"C:\\a.png\\notes", L"") == FileKind::Other && LowerExtension(L"C:\\a.b\\file").empty(), "kind: a dot in a folder name is no extension");
    Expect(KindOf(L"C:\\d\\report.pdf", L"application/pdf") == FileKind::Other && GlyphFor(FileKind::Other) == nullptr, "kind: anything else keeps the shell icon");
    Expect(CleanMime(L" Image/PNG ; q=1") == L"image/png" && CleanMime(L"text\thtml/x").empty() && CleanMime(L"a/b\nc").empty() && CleanMime(L"plain").empty() &&
           CleanMime(std::wstring(L"image/") + std::wstring(100, L'x')).empty() && CleanMime(std::wstring(L"image/p\0ng", 10)).empty(), "MIME: type only, token characters, bounded");
    // History: the MIME type rides as a trailing field, older files (and builds) still read.
    std::vector<Item> items(2);
    items[0].state = State::Completed; items[0].path = L"C:\\d\\photo"; items[0].total = 10; items[0].mime = L"image/png";
    items[1].state = State::Completed; items[1].path = L"C:\\d\\notes.txt"; items[1].total = 5;
    std::stringstream io; WriteHistory(io, items); const std::string text = io.str();
    Expect(text.find("\timage/png\n") != std::string::npos && text.find("notes.txt\t\t\n") != std::string::npos, "history: the MIME type is the last field, only when known");
    int next = 1; std::istringstream in(text); auto back = ReadHistory(in, next);
    Expect(back.size() == 2 && back[0].mime == L"image/png" && back[1].mime.empty() && back[0].path == items[0].path, "history: the MIME type round-trips");
    next = 1; std::istringstream junk("browser-downloads 2\nC\t9\t1\tC:\\d\\x\t\t\tnot a mime\tmore\n"); auto j = ReadHistory(junk, next);
    Expect(j.size() == 1 && j[0].mime.empty(), "history: a malformed MIME field is dropped, the row kept");
    next = 1; std::istringstream v1("browser-downloads 1\nC\t9\tC:\\d\\old.png\thttps://x.test\t\n"); auto old = ReadHistory(v1, next);
    Expect(old.size() == 1 && old[0].mime.empty() && KindOf(old[0].path, old[0].mime) == FileKind::Image, "history: rows without a MIME type still get their kind from the extension");
}

static void GlyphChecks() {
    // Both glyphs draw: every path parses into a geometry and is filled or stroked.
    ID2D1Factory* factory = nullptr; D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
    for (auto* svg : {FileGlyphs::image, FileGlyphs::vector}) {
        const auto shape = ParseGlyph(svg); bool draws = shape.view > 0 && !shape.paths.empty();
        for (auto& p : shape.paths) { auto* g = factory ? DialogKit::SvgGeometry(factory, p.d.c_str(), p.evenOdd) : nullptr; draws = draws && g && (p.fill || p.stroke); if (g) g->Release(); }
        Expect(draws, svg == FileGlyphs::image ? "glyph image: every path parses and draws" : "glyph vector-path: every path parses and draws");
    }
    if (factory) factory->Release();
    // This copy's Jiggicons outlines (the public copy draws Phosphor substitutes instead, filled on their 256 grid:
    // tools/public-mirror/paid-icons; checked below on a sample).
    const auto image = ParseGlyph(FileGlyphs::image), vector = ParseGlyph(FileGlyphs::vector);
    if (image.view == 24) {
        bool lines = image.paths.size() == 3; for (auto& p : image.paths) lines = lines && p.stroke && !p.fill;
        Expect(image.strokeWidth == 1.5f && lines, "glyph image: three 1.5 strokes on the 24 grid");
        bool parts = vector.view == 24 && vector.paths.size() == 5;
        for (size_t i = 0; parts && i < vector.paths.size(); ++i) parts = i < 2 ? (vector.paths[i].fill && vector.paths[i].evenOdd && !vector.paths[i].stroke) : (vector.paths[i].stroke && !vector.paths[i].fill);
        Expect(parts, "glyph vector-path: two filled curves (even-odd), three stroked anchors");
    }
    const auto phosphor = ParseGlyph("<svg viewBox=\"0 0 256 256\" fill=\"currentColor\"><path d=\"M40,36H216a20,20,0,0,1,20,20V200Z\"/></svg>");
    Expect(phosphor.view == 256 && phosphor.paths.size() == 1 && phosphor.paths[0].fill && !phosphor.paths[0].stroke, "glyph: a Phosphor substitute parses (filled, 256 grid)");
    Expect(ParseGlyph(nullptr).view == 0 && ParseGlyph("<svg>").view == 0 && ParseGlyph("<svg viewBox=\"0 0 24 12\"><path d=\"M0 0Z\"/></svg>").view == 0 &&
           ParseGlyph("<g viewBox=\"0 0 16 16\"><path d=\"M0 0Z\"/></g>").view == 0 && ParseGlyph("<svg viewBox=\"0 0 16 16\"></svg>").view == 0, "glyph: anything else draws nothing");
}

// ---- Test images (written with WIC's encoders into the output folder) ------------------------------------------
struct Rgb { BYTE r, g, b; };
static bool Near(const BYTE* px, Rgb c, int tolerance = 70) { return std::abs(px[2] - c.r) <= tolerance && std::abs(px[1] - c.g) <= tolerance && std::abs(px[0] - c.b) <= tolerance; }
static const Rgb red{255, 0, 0}, blue{0, 0, 255}, green{0, 200, 0}, white{255, 255, 255};
// Left half `left`, right half `right`; with `bottomLeft` / `bottomRight`, four quadrants.
static bool WriteImage(IWICImagingFactory* wic, const std::wstring& path, const GUID& container, UINT w, UINT h, Rgb left, Rgb right, int orientation = 0, const Rgb* bottomLeft = nullptr, const Rgb* bottomRight = nullptr) {
    std::vector<BYTE> px(static_cast<size_t>(w) * h * 4);
    for (UINT y = 0; y < h; ++y) for (UINT x = 0; x < w; ++x) {
        const bool low = bottomLeft && y >= h / 2; const Rgb c = x < w / 2 ? (low ? *bottomLeft : left) : (low ? *bottomRight : right);
        BYTE* p = &px[(static_cast<size_t>(y) * w + x) * 4]; p[0] = c.b; p[1] = c.g; p[2] = c.r; p[3] = 255;
    }
    Th::Ref<IWICBitmap> bitmap; Th::Ref<IWICStream> stream; Th::Ref<IWICBitmapEncoder> encoder; Th::Ref<IWICBitmapFrameEncode> frame; Th::Ref<IPropertyBag2> props;
    if (FAILED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, w * 4, static_cast<UINT>(px.size()), px.data(), &bitmap))) return false;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    if (FAILED(wic->CreateEncoder(container, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache))) return false;
    if (FAILED(encoder->CreateNewFrame(&frame, &props)) || FAILED(frame->Initialize(props.p)) || FAILED(frame->SetSize(w, h))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA; if (FAILED(frame->SetPixelFormat(&format))) return false;
    if (orientation) {
        Th::Ref<IWICMetadataQueryWriter> writer; PROPVARIANT value; PropVariantInit(&value); value.vt = VT_UI2; value.uiVal = static_cast<USHORT>(orientation);
        if (FAILED(frame->GetMetadataQueryWriter(&writer)) || FAILED(writer->SetMetadataByName(L"/app1/ifd/{ushort=274}", &value))) return false;
    }
    IWICBitmapSource* source = bitmap.p; Th::Ref<IWICFormatConverter> converter; Th::Ref<IWICPalette> palette;
    if (format != GUID_WICPixelFormat32bppBGRA) {
        const bool indexed = format == GUID_WICPixelFormat8bppIndexed;
        if (indexed && (FAILED(wic->CreatePalette(&palette)) || FAILED(palette->InitializePredefined(WICBitmapPaletteTypeFixedWebPalette, FALSE)) || FAILED(frame->SetPalette(palette.p)))) return false;
        if (FAILED(wic->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(bitmap.p, format, WICBitmapDitherTypeNone, indexed ? palette.p : nullptr, 0, indexed ? WICBitmapPaletteTypeFixedWebPalette : WICBitmapPaletteTypeCustom))) return false;
        source = converter.p;
    }
    return SUCCEEDED(frame->WriteSource(source, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
static std::string ReadAll(const std::wstring& path) { std::ifstream f(path.c_str(), std::ios::binary); std::ostringstream o; o << f.rdbuf(); return o.str(); }
static bool WriteAll(const std::wstring& path, const std::string& bytes) { std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc); f.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); return static_cast<bool>(f); }
static std::uint32_t Crc32(const std::string& bytes) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : bytes) { c ^= b; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static std::string Be32(std::uint32_t v) { return {static_cast<char>(v >> 24), static_cast<char>(v >> 16), static_cast<char>(v >> 8), static_cast<char>(v)}; }
static std::string Chunk(const char* type, const std::string& data) { const std::string body = std::string(type, 4) + data; return Be32(static_cast<std::uint32_t>(data.size())) + body + Be32(Crc32(body)); }
// An icon file holding two PNG images: 16 x 16 and 32 x 32.
static std::string Ico(const std::string& small, const std::string& large) {
    auto le16 = [](unsigned v) { return std::string{static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)}; };
    auto le32 = [](std::uint32_t v) { return std::string{static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16), static_cast<char>(v >> 24)}; };
    std::string out = le16(0) + le16(1) + le16(2); const std::uint32_t first = 6 + 2 * 16;
    out += std::string{16, 16, 0, 0} + le16(1) + le16(32) + le32(static_cast<std::uint32_t>(small.size())) + le32(first);
    out += std::string{32, 32, 0, 0} + le16(1) + le16(32) + le32(static_cast<std::uint32_t>(large.size())) + le32(first + static_cast<std::uint32_t>(small.size()));
    return out + small + large;
}
static const BYTE* At(const Th::Pixels& p, int x, int y) { return &p.bgra[(static_cast<size_t>(y) * p.side + x) * 4]; }

static void DecodeChecks(const std::wstring& dir) {
    Th::Ref<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&wic)))) { Expect(false, "WIC factory"); return; }
    const std::wstring png = dir + L"\\photo.png", jpg = dir + L"\\photo.jpg", turned = dir + L"\\turned.jpg", gif = dir + L"\\anim.gif", bmp = dir + L"\\paint.bmp", tif = dir + L"\\scan.tiff";
    Expect(WriteImage(wic.p, png, GUID_ContainerFormatPng, 40, 20, red, blue) && WriteImage(wic.p, jpg, GUID_ContainerFormatJpeg, 40, 20, red, blue) &&
           WriteImage(wic.p, turned, GUID_ContainerFormatJpeg, 40, 20, red, blue, 6) && WriteImage(wic.p, gif, GUID_ContainerFormatGif, 40, 20, red, blue) &&
           WriteImage(wic.p, bmp, GUID_ContainerFormatBmp, 40, 20, red, blue) && WriteImage(wic.p, tif, GUID_ContainerFormatTiff, 40, 20, red, blue), "test images written (png, jpg, jpg turned, gif, bmp, tiff)");
    // Centre square of a 40 x 20 picture (red | blue), scaled to 24: red on the left, blue on the right.
    for (const auto* path : {&png, &jpg, &gif, &bmp, &tif}) {
        Th::Pixels p; const auto outcome = Th::Decode(wic.p, *path, 24, p);
        const bool ok = outcome == Th::Outcome::Ok && p.side == 24 && p.bgra.size() == 24 * 24 * 4 && Near(At(p, 3, 12), red) && Near(At(p, 20, 12), blue) && At(p, 3, 12)[3] == 255;
        Expect(ok, ("decoded: centre square, scaled to 24 px, opaque (" + std::string(path->begin() + static_cast<std::ptrdiff_t>(path->rfind(L'.')), path->end()) + ")").c_str());
    }
    { Th::Pixels p; Expect(Th::Decode(wic.p, turned, 24, p) == Th::Outcome::Ok && Near(At(p, 12, 3), red) && Near(At(p, 12, 20), blue), "decoded: EXIF orientation 6 turns the picture upright (left half on top)"); }
    // Every EXIF orientation, on a 40 x 40 picture stored red | blue over green | white: the displayed quadrants
    // (top-left, top-right, bottom-left, bottom-right) per the EXIF definition of tags 1..8.
    {
        const Rgb* q[4] = {&red, &blue, &green, &white};   // stored TL, TR, BL, BR
        const int shown[9][4] = {{}, {0, 1, 2, 3}, {1, 0, 3, 2}, {3, 2, 1, 0}, {2, 3, 0, 1}, {0, 2, 1, 3}, {2, 0, 3, 1}, {3, 1, 2, 0}, {1, 3, 0, 2}};
        for (int o = 1; o <= 8; ++o) {
            const std::wstring path = dir + L"\\orientation-" + std::to_wstring(o) + L".jpg"; Th::Pixels p;
            const bool ok = WriteImage(wic.p, path, GUID_ContainerFormatJpeg, 40, 40, red, blue, o, &green, &white) && Th::Decode(wic.p, path, 24, p) == Th::Outcome::Ok &&
                            Near(At(p, 5, 5), *q[shown[o][0]]) && Near(At(p, 18, 5), *q[shown[o][1]]) && Near(At(p, 5, 18), *q[shown[o][2]]) && Near(At(p, 18, 18), *q[shown[o][3]]);
            Expect(ok, ("decoded: EXIF orientation " + std::to_string(o) + " shown upright").c_str());
        }
    }
    { Th::Pixels p; Expect(Th::Decode(wic.p, png, 96, p) == Th::Outcome::Ok && p.side == 96 && Th::Decode(wic.p, png, Th::maxSide + 1, p) != Th::Outcome::Ok, "decoded at the tile's pixels (96 at 400 %), never above the cap"); }
    // Icon: its largest image.
    {
        const std::wstring s = dir + L"\\ico-16.png", l = dir + L"\\ico-32.png", ico = dir + L"\\site.ico";
        const bool written = WriteImage(wic.p, s, GUID_ContainerFormatPng, 16, 16, green, green) && WriteImage(wic.p, l, GUID_ContainerFormatPng, 32, 32, red, blue) && WriteAll(ico, Ico(ReadAll(s), ReadAll(l)));
        Th::Pixels p; Expect(written && Th::Decode(wic.p, ico, 24, p) == Th::Outcome::Ok && Near(At(p, 3, 12), red) && Near(At(p, 20, 12), blue), "decoded: an icon file shows its largest image");
    }
    // Fallbacks: the row shows the glyph.
    const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\"><rect width=\"40\" height=\"40\" fill=\"#f00\"/><script>alert(1)</script></svg>";
    const std::wstring trapPng = dir + L"\\trap.png", trapSvg = dir + L"\\drawing.svg", empty = dir + L"\\empty.png", truncated = dir + L"\\truncated.jpg", garbage = dir + L"\\garbage.png",
                       bomb = dir + L"\\bomb.png", exe = dir + L"\\setup.png", huge = dir + L"\\huge.png";
    const std::string jpeg = ReadAll(jpg);
    std::string ihdr = Be32(20000) + Be32(20000) + std::string{8, 6, 0, 0, 0};
    WriteAll(trapPng, svg); WriteAll(trapSvg, "<?xml version=\"1.0\"?>\n" + svg); WriteAll(empty, ""); WriteAll(truncated, jpeg.substr(0, 40));
    WriteAll(garbage, std::string("\x89PNG\r\n\x1a\n", 8) + std::string(200, '\x5a')); WriteAll(bomb, std::string("\x89PNG\r\n\x1a\n", 8) + Chunk("IHDR", ihdr) + Chunk("IDAT", std::string("\x78\x9c\x03\x00\x00\x00\x00\x01", 8)) + Chunk("IEND", ""));   // 8 bytes of zlib: what a bomb costs
    WriteAll(exe, std::string("MZ\x90\0\x03\0\0\0", 8) + std::string(300, '\0'));
    {   // 65 MiB, sparse-fast: only its size is ever looked at
        HANDLE h = CreateFileW(huge.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER at; at.QuadPart = Th::maxBytes + (1 << 20); DWORD wrote = 0;
        const bool made = h != INVALID_HANDLE_VALUE && WriteFile(h, "\x89PNG\r\n\x1a\n", 8, &wrote, nullptr) && SetFilePointerEx(h, at, nullptr, FILE_BEGIN) && SetEndOfFile(h);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        Expect(made, "huge test file written");
    }
    auto outcome = [&](const std::wstring& path) { Th::Pixels p; const auto o = Th::Decode(wic.p, path, 24, p); return o != Th::Outcome::Ok && !p.bgra.empty() ? Th::Outcome::Ok : o; };   // pixels only with Ok
    Expect(outcome(dir + L"\\nothing-here.png") == Th::Outcome::Missing && outcome(dir) == Th::Outcome::Missing, "fallback: a missing file or a folder");
    Expect(outcome(empty) == Th::Outcome::Empty, "fallback: an empty file");
    Expect(outcome(huge) == Th::Outcome::TooLarge, "fallback: a file above 64 MiB is not read"); DeleteFileW(huge.c_str());
    Expect(outcome(bomb) == Th::Outcome::TooLarge, "fallback: a picture above 100 megapixels (20000 x 20000 header) is not decoded");
    Expect(outcome(garbage) != Th::Outcome::Ok && outcome(truncated) != Th::Outcome::Ok, "fallback: a corrupt or truncated file");
    Expect(outcome(trapPng) == Th::Outcome::NotImage && outcome(trapSvg) == Th::Outcome::NotImage, "SVG: never decoded, whatever its name (by content)");
    Expect(outcome(exe) == Th::Outcome::NotImage, "fallback: a program named .png is no image");
    const unsigned char svgBytes[] = "<svg xmlns=\"http://www.w3.org/2000/svg\"/>";
    Expect(Th::Sniff(svgBytes, sizeof svgBytes - 1) == Th::Format::None && Th::DecoderOf(Th::Format::None) == nullptr, "SVG: no signature, no decoder");
    const unsigned char webp[] = "RIFF\x10\0\0\0WEBPVP8 ", avif[] = "\0\0\0\x1c" "ftypavif\0\0\0\0";
    Expect(Th::Sniff(webp, 16) == Th::Format::Webp && Th::Sniff(avif, 16) == Th::Format::Heif && Th::DecoderOf(Th::Format::Webp) && Th::DecoderOf(Th::Format::Heif), "sniff: WebP and AVIF go to their own system decoders");

    // The service: asynchronous, cached, re-checked against the file.
    auto& service = Th::Service::Get();
    Expect(service.Lookup(png, 24, nullptr, 0) == nullptr, "service: the first lookup returns at once (decoded on the worker)");
    Expect(service.WaitIdleForTest(20000), "service: the worker finishes");
    auto thumb = service.Lookup(png, 24, nullptr, 0); const auto decodes = service.DecodesForTest();
    Expect(thumb && thumb->ok && thumb->pixels.side == 24 && Near(At(thumb->pixels, 3, 12), red), "service: the thumbnail is ready");
    Expect(service.Lookup(png, 24, nullptr, 0) == thumb && service.DecodesForTest() == decodes, "service: cached (no second decode)");
    service.Recheck(png, nullptr, 0); service.WaitIdleForTest(20000);
    Expect(service.Lookup(png, 24, nullptr, 0) == thumb && service.DecodesForTest() == decodes, "service: re-checked, unchanged file: not decoded again");
    Expect(WriteImage(wic.p, png, GUID_ContainerFormatPng, 30, 30, green, green), "the png replaced on disk");
    { HANDLE h = CreateFileW(png.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); FILETIME t{}; GetSystemTimeAsFileTime(&t); ULARGE_INTEGER u{{t.dwLowDateTime, t.dwHighDateTime}}; u.QuadPart += 36000000000ULL; t = {u.LowPart, u.HighPart}; if (h != INVALID_HANDLE_VALUE) { SetFileTime(h, nullptr, nullptr, &t); CloseHandle(h); } }
    service.Recheck(png, nullptr, 0); service.WaitIdleForTest(20000);
    auto fresh = service.Lookup(png, 24, nullptr, 0);
    Expect(fresh && fresh != thumb && fresh->ok && Near(At(fresh->pixels, 12, 12), green) && service.DecodesForTest() == decodes + 1, "service: a changed file is decoded again when the panel opens");
    service.Lookup(trapPng, 24, nullptr, 0); service.WaitIdleForTest(20000);
    auto failed = service.Lookup(trapPng, 24, nullptr, 0); const auto after = service.DecodesForTest();
    Expect(failed && !failed->ok && failed->outcome == Th::Outcome::NotImage && service.Lookup(trapPng, 24, nullptr, 0) == failed && service.DecodesForTest() == after, "service: a failure is cached too (the glyph, no retry loop)");
    Expect(service.Lookup(L"", 24, nullptr, 0) == nullptr && service.Lookup(png, 0, nullptr, 0) == nullptr, "service: nothing to decode for an empty path or size");
}

// ---- The popover off screen: png (done), jpg (in progress), svg (done), zip (done) ----------------------------------
static bool ReadPng(const std::wstring& path, UINT& w, UINT& h, std::vector<BYTE>& px) {
    Th::Ref<IWICImagingFactory> wic; Th::Ref<IWICBitmapDecoder> dec; Th::Ref<IWICBitmapFrameDecode> frame; Th::Ref<IWICFormatConverter> conv;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&wic)))) return false;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) || FAILED(dec->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h))) return false;
    if (FAILED(wic->CreateFormatConverter(&conv)) || FAILED(conv->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    px.resize(static_cast<size_t>(w) * h * 4); return SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data()));
}
static void RenderChecks(const std::wstring& root, const std::wstring& dir) {
    const std::wstring png = dir + L"\\vacances.png", svg = dir + L"\\drawing.svg";
    {
        Th::Ref<IWICImagingFactory> wic; CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&wic));
        Expect(wic && WriteImage(wic.p, png, GUID_ContainerFormatPng, 64, 48, red, blue), "render: the png written");
    }
    std::vector<Item> rows(4);
    rows[0].id = 1; rows[0].path = png; rows[0].state = State::Completed; rows[0].received = rows[0].total = 2048; rows[0].mime = L"image/png";
    rows[1].id = 2; rows[1].path = dir + L"\\glacier.jpg"; rows[1].state = State::InProgress; rows[1].received = 1500000; rows[1].total = 4200000; rows[1].endTime = 1014000;
    rows[2].id = 3; rows[2].path = svg; rows[2].state = State::Completed; rows[2].received = rows[2].total = 512;
    rows[3].id = 4; rows[3].path = L"C:\\d\\archive.zip"; rows[3].state = State::Completed; rows[3].received = rows[3].total = 9000000;
    for (UINT dpi : {96u, 120u, 192u}) {
        const int side = static_cast<int>(std::lround(Panel::fileIcon * dpi / 96.f));
        auto& service = Th::Service::Get();
        service.Lookup(png, side, nullptr, 0); service.WaitIdleForTest(20000);   // the panel would repaint when it is ready
        const auto slots = DownloadsUI::PopoverSlots(rows); const float height = DownloadsUI::PopoverHeight(slots);
        Chrome chrome; if (FAILED(chrome.InitOffscreen(root, dpi, Panel::width, height))) { Expect(false, "render: offscreen chrome"); return; }
        DownloadsUI::View view(chrome); std::map<int, RowReveal> reveals;
        DownloadsUI::Scene scene; scene.items = &rows; scene.reveals = &reveals; scene.now = Motion::Time{} + std::chrono::seconds(100); scene.nowMs = 1000000; scene.viewBottom = height - Panel::padding / 2;
        auto* target = chrome.RenderTarget(); target->BeginDraw(); view.Paint(scene, slots, height); const HRESULT hr = target->EndDraw(); view.Release();
        const std::wstring name = dir + L"\\file-kinds-" + std::to_wstring(dpi) + L".png";
        Expect(SUCCEEDED(hr) && SUCCEEDED(chrome.SavePng(name.c_str())), "render: popover with a png, a jpg in progress, an svg and a zip");
        chrome.Dispose();
        UINT w = 0, h = 0; std::vector<BYTE> px; if (!ReadPng(name, w, h, px)) { Expect(false, "render: read back"); continue; }
        const float k = dpi / 96.f;
        auto pixel = [&](float x, float y) { const int ix = std::min<int>(static_cast<int>(w) - 1, static_cast<int>(x * k)), iy = std::min<int>(static_cast<int>(h) - 1, static_cast<int>(y * k)); return &px[(static_cast<size_t>(iy) * w + ix) * 4]; };
        // Icon box of row i: 24 DIP centred in the 32 DIP tile.
        auto box = [&](int i) { const float top = slots[static_cast<size_t>(i)].top; return D2D1::RectF(Panel::padding + 4, top + 4, Panel::padding + 28, top + 28); };
        auto scan = [&](int i, auto test) { const auto b = box(i); int n = 0; for (float y = b.top; y < b.bottom; y += 1 / k) for (float x = b.left; x < b.right; x += 1 / k) if (test(pixel(x, y))) ++n; return n; };
        auto isRed = [](const BYTE* p) { return p[2] > 150 && p[1] < 90 && p[0] < 90; };
        auto bright = [](int level) { return [level](const BYTE* p) { return p[0] > level && p[1] > level && p[2] > level && std::abs(p[0] - p[2]) < 12; }; };
        const auto b0 = box(0);
        Expect(isRed(pixel(b0.left + 4, (b0.top + b0.bottom) / 2)) && Near(pixel(b0.right - 4, (b0.top + b0.bottom) / 2), blue), (std::string("render: the png row shows the picture itself @") + std::to_string(dpi)).c_str());
        Expect(scan(1, bright(70)) > 10 && scan(1, isRed) == 0, (std::string("render: the jpg being downloaded shows the image glyph (faded) @") + std::to_string(dpi)).c_str());
        Expect(scan(2, bright(100)) > 10 && scan(2, isRed) == 0, (std::string("render: the svg row shows the vector glyph, never the file's red @") + std::to_string(dpi)).c_str());
        // Designer: the glyph is solid #949494 (its fully covered pixels; nothing lighter, no tint).
        int lightest = 0; bool grey = true;
        scan(2, [&](const BYTE* p) { lightest = std::max<int>(lightest, std::max({p[0], p[1], p[2]})); grey = grey && std::abs(p[0] - p[2]) < 4 && std::abs(p[1] - p[2]) < 4; return false; });
        Expect(lightest >= 0x94 - 3 && lightest <= 0x94 + 3 && grey, (std::string("render: the glyph is solid #949494 @") + std::to_string(dpi)).c_str());
    }
    Expect(Th::Service::Get().KnownForTest(png) && !Th::Service::Get().KnownForTest(svg), "SVG: the panel never asked for the svg to be decoded");
    // Designer: a dark picture keeps its edge: a 1 px inner ring #FFFFFF14 over the thumbnail (black here).
    const std::wstring dark = dir + L"\\nuit.png";
    {
        Th::Ref<IWICImagingFactory> wic; CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&wic));
        Expect(wic && WriteImage(wic.p, dark, GUID_ContainerFormatPng, 48, 48, {0, 0, 0}, {0, 0, 0}), "render: a black png written");
    }
    // Second row: a finished .png that cannot be decoded shows the image glyph at full strength. Its strokes overlap
    // where the hills meet the frame: a translucent white would get lighter there, the solid #949494 does not.
    const std::wstring broken = dir + L"\\cassee.png"; WriteAll(broken, std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, '\x33'));
    std::vector<Item> night(2); night[0].id = 1; night[0].path = dark; night[0].state = State::Completed; night[0].received = night[0].total = 900;
    night[1].id = 2; night[1].path = broken; night[1].state = State::Completed; night[1].received = night[1].total = 72;
    for (UINT dpi : {96u, 120u, 192u}) {
        auto& service = Th::Service::Get(); const int side = static_cast<int>(std::lround(Panel::fileIcon * dpi / 96.f));
        service.Lookup(dark, side, nullptr, 0); service.Lookup(broken, side, nullptr, 0); service.WaitIdleForTest(20000);
        const auto slots = DownloadsUI::PopoverSlots(night); const float height = DownloadsUI::PopoverHeight(slots);
        Chrome chrome; if (FAILED(chrome.InitOffscreen(root, dpi, Panel::width, height))) { Expect(false, "render: offscreen chrome"); return; }
        DownloadsUI::View view(chrome); std::map<int, RowReveal> reveals;
        DownloadsUI::Scene scene; scene.items = &night; scene.reveals = &reveals; scene.now = Motion::Time{} + std::chrono::seconds(100); scene.nowMs = 1000000; scene.viewBottom = height - Panel::padding / 2;
        auto* target = chrome.RenderTarget(); target->BeginDraw(); view.Paint(scene, slots, height); const HRESULT hr = target->EndDraw(); view.Release();
        const std::wstring name = dir + L"\\dark-thumbnail-" + std::to_wstring(dpi) + L".png";
        const bool saved = SUCCEEDED(hr) && SUCCEEDED(chrome.SavePng(name.c_str())); chrome.Dispose();
        UINT w = 0, h = 0; std::vector<BYTE> px; if (!saved || !ReadPng(name, w, h, px)) { Expect(false, "render: dark thumbnail"); continue; }
        const float k = dpi / 96.f, top = slots[0].top + 4, left = Panel::padding + 4, mid = top + 12;
        auto at = [&](int x, int y) { return &px[(static_cast<size_t>(y) * w + x) * 4]; };
        const int edgeX = static_cast<int>(std::lround(left * k)), edgeY = static_cast<int>(std::lround(top * k)), midY = static_cast<int>(mid * k), midX = static_cast<int>((left + 12) * k);
        const BYTE *edge = at(edgeX, midY), *topEdge = at(midX, edgeY), *inside = at(edgeX + 2, midY), *centre = at(midX, midY);
        auto ring = [](const BYTE* p) { return p[0] >= 0x14 - 6 && p[0] <= 0x14 + 6 && p[0] == p[1] && p[1] == p[2]; };
        Expect(ring(edge) && ring(topEdge) && inside[0] < 6 && centre[0] < 6, (std::string("render: a black thumbnail keeps a 1 px #FFFFFF14 inner ring, its inside black @") + std::to_string(dpi)).c_str());
        int lightest = 0; const float glyphTop = slots[1].top + 4;
        for (int y = static_cast<int>(glyphTop * k); y < static_cast<int>((glyphTop + 24) * k); ++y)
            for (int x = static_cast<int>(left * k); x < static_cast<int>((left + 24) * k); ++x) lightest = std::max<int>(lightest, at(x, y)[1]);
        Expect(lightest >= 0x94 - 3 && lightest <= 0x94 + 3, (std::string("render: the image glyph is solid #949494, overlaps included @") + std::to_string(dpi)).c_str());
    }
}

int wmain(int argc, wchar_t** argv) {
    I18n::SetForProcess(I18n::Locale::Fr);
    KindChecks();
    GlyphChecks();
    if (argc >= 3) {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
        const std::wstring dir = std::wstring(argv[2]) + L"\\file-kinds";
        CreateDirectoryW(argv[2], nullptr); CreateDirectoryW(dir.c_str(), nullptr);
        DecodeChecks(dir);
        RenderChecks(argv[1], dir);
        CoUninitialize();
    }
    if (failures) { std::fprintf(stderr, "%d download file-kind check(s) failed\n", failures); return 1; }
    std::printf("download file-kind checks passed\n");
    return 0;
}
