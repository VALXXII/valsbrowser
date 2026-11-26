#pragma once
// Download rows: what a file shows in its tile (owner's request, 2026-11-26). The shell's icon for a type is
// the icon of the application associated with it, so an SVG or a PNG showed an editor's logo. Instead:
//  - an image (png, jpg, gif, webp, avif, bmp, ico, tiff, heic): a thumbnail of the file itself once it is
//    downloaded (src/download-thumbnails.h: by content, off the UI thread, system codecs only, cached), the
//    Jiggicons « image » glyph until then or when it cannot be decoded;
//  - an SVG: the Jiggicons « vector-path » glyph. The file is never rendered: it is untrusted markup;
//  - anything else: the shell's icon, as before.
// The extension of the file on disk decides (it is what Windows opens it with); the MIME type the server
// sent decides only for a file without one, and an SVG MIME type keeps an image-named file from ever being
// decoded. Neither ever turns another extension (an .exe served as image/png) into an image.
// No Paper board draws file-type icons (the downloads boards show the extension in text): glyphs and
// thumbnails take the shell icon's place and size, 24 DIP in the 32 DIP tile (designer's OK, 2026-11-26): glyphs in
// solid #949494, thumbnails with 3 DIP corners and a 1 px inner ring #FFFFFF14.
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace NativeChrome {
namespace Downloads {
enum class FileKind { Other, Image, Vector };
// ".PNG" of "C:\d\photo.PNG" → ".png"; "" without one (a dot in a folder name is not one).
inline std::wstring LowerExtension(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/"), dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return ext;
}
// " Image/SVG+XML; charset=utf-8" → "image/svg+xml".
inline std::wstring LowerMime(const std::wstring& mime) {
    std::wstring type = mime.substr(0, mime.find(L';'));
    const auto first = type.find_first_not_of(L" \t"), last = type.find_last_not_of(L" \t");
    type = first == std::wstring::npos ? L"" : type.substr(first, last - first + 1);
    for (auto& c : type) c = static_cast<wchar_t>(std::towlower(c));
    return type;
}
// What is kept of a server's MIME type (stored in the history, one tab-separated field): its type only, and
// only when it is made of token characters (RFC 9110), at most 100 of them; "" otherwise.
inline std::wstring CleanMime(const std::wstring& mime) {
    const std::wstring type = LowerMime(mime);
    if (type.size() > 100 || type.find(L'/') == std::wstring::npos) return L"";
    for (wchar_t c : type) if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || (c && std::wcschr(L"!#$%&'*+-.^_`|~/", c)))) return L"";
    return type;
}
inline FileKind KindOf(const std::wstring& path, const std::wstring& mime) {
    static const wchar_t* const vectors[] = {L".svg", L".svgz"};
    static const wchar_t* const images[] = {L".png", L".apng", L".jpg", L".jpeg", L".jpe", L".jfif", L".pjpeg", L".pjp", L".gif", L".webp",
                                            L".avif", L".bmp", L".dib", L".ico", L".tif", L".tiff", L".heic", L".heif"};
    const auto ext = LowerExtension(path), type = LowerMime(mime);
    const bool svgType = type == L"image/svg+xml";
    for (auto* v : vectors) if (ext == v) return FileKind::Vector;
    for (auto* i : images) if (ext == i) return svgType ? FileKind::Vector : FileKind::Image;
    if (!ext.empty()) return FileKind::Other;
    if (svgType) return FileKind::Vector;
    return type.rfind(L"image/", 0) == 0 && type.size() > 6 ? FileKind::Image : FileKind::Other;
}

// ---- Glyphs (Jiggicons, outline variant, 24 x 24 grid) --------------------------------------------------------
// A small SVG subset drawn by the panel (View::Glyph): the root's viewBox (square, from 0), fill, stroke and
// stroke-width; each <path>'s d, fill, stroke and fill-rule. Strokes have round caps and joins.
// Paid Jiggicons icons: each one line, declared in tools/public-mirror/paid-icons (the public copy draws Phosphor).
namespace FileGlyphs {
inline constexpr const char* image = "<svg viewBox=\"0 0 256 256\" fill=\"currentColor\"><path d=\"M144,96a16,16,0,1,1,16,16A16,16,0,0,1,144,96Zm92-40V200a20,20,0,0,1-20,20H40a20,20,0,0,1-20-20V56A20,20,0,0,1,40,36H216A20,20,0,0,1,236,56ZM44,60v79.72l33.86-33.86a20,20,0,0,1,28.28,0L147.31,147l17.18-17.17a20,20,0,0,1,28.28,0L212,149.09V60Zm0,136H162.34L92,125.66l-48,48Zm168,0V183l-33.37-33.37L164.28,164l32,32Z\"/></svg>";
inline constexpr const char* vector = "<svg viewBox=\"0 0 256 256\" fill=\"currentColor\"><path d=\"M224.28,141a100.61,100.61,0,0,0-31.4-49H240a12,12,0,0,0,0-24H161.94a36,36,0,0,0-67.88,0H16a12,12,0,0,0,0,24H63.12a100.61,100.61,0,0,0-31.4,49A36,36,0,1,0,56,143.74,76.66,76.66,0,0,1,97.15,98.53a36,36,0,0,0,61.7,0A76.66,76.66,0,0,1,200,143.74,36,36,0,1,0,224.28,141ZM40,188a12,12,0,1,1,12-12A12,12,0,0,1,40,188Zm88-96a12,12,0,1,1,12-12A12,12,0,0,1,128,92Zm88,96a12,12,0,1,1,12-12A12,12,0,0,1,216,188Z\"/></svg>";
}
inline const char* GlyphFor(FileKind kind) { return kind == FileKind::Image ? FileGlyphs::image : kind == FileKind::Vector ? FileGlyphs::vector : nullptr; }

struct GlyphPath { std::string d; bool fill = false, stroke = false, evenOdd = false; };
struct GlyphShape { float view = 0, strokeWidth = 0; std::vector<GlyphPath> paths; };
// ` name="value"` inside one tag; false when absent.
inline bool GlyphAttribute(const std::string& tag, const char* name, std::string& value) {
    const std::string key = std::string(" ") + name + "=\"";
    const auto at = tag.find(key); if (at == std::string::npos) return false;
    const auto start = at + key.size(), end = tag.find('"', start); if (end == std::string::npos) return false;
    value = tag.substr(start, end - start); return true;
}
// Empty shape (view 0) when the markup is not that subset.
inline GlyphShape ParseGlyph(const char* svg) {
    GlyphShape shape; if (!svg) return shape;
    const std::string text = svg;
    if (text.rfind("<svg", 0) != 0) return shape;
    const auto rootEnd = text.find('>'); if (rootEnd == std::string::npos) return shape;
    const std::string root = text.substr(0, rootEnd);
    std::string box, rootFill = "currentColor", rootStroke = "none", width;
    if (!GlyphAttribute(root, "viewBox", box)) return shape;
    float v[4] = {}; const char* p = box.c_str();
    for (auto& n : v) { char* end = nullptr; n = std::strtof(p, &end); if (end == p) return shape; p = end; }
    if (v[0] != 0 || v[1] != 0 || v[2] <= 0 || v[2] != v[3]) return shape;
    GlyphAttribute(root, "fill", rootFill); GlyphAttribute(root, "stroke", rootStroke);
    if (GlyphAttribute(root, "stroke-width", width)) shape.strokeWidth = std::strtof(width.c_str(), nullptr);
    for (auto at = text.find("<path", rootEnd); at != std::string::npos; at = text.find("<path", at + 5)) {
        const auto end = text.find("/>", at); if (end == std::string::npos) return GlyphShape{};
        const std::string tag = text.substr(at, end - at);
        GlyphPath path; std::string fill = rootFill, stroke = rootStroke, rule;
        if (!GlyphAttribute(tag, "d", path.d) || path.d.empty()) return GlyphShape{};
        GlyphAttribute(tag, "fill", fill); GlyphAttribute(tag, "stroke", stroke);
        path.fill = fill != "none"; path.stroke = stroke != "none" && shape.strokeWidth > 0;
        path.evenOdd = GlyphAttribute(tag, "fill-rule", rule) && rule == "evenodd";
        shape.paths.push_back(std::move(path));
    }
    if (shape.paths.empty()) return GlyphShape{};
    shape.view = v[2];
    return shape;
}
} // namespace Downloads
} // namespace NativeChrome
