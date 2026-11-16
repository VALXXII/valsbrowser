#pragma once
// « Importer depuis un navigateur » — every browser this PC knows about, each with its own small logo
// (design/browser-data-import-plan.md, part 3). METADATA ONLY:
//  - Installed browsers come from the registration Windows itself uses to list them (« Applications par défaut »):
//    SOFTWARE\Clients\StartMenuInternet under HKCU and HKLM (both registry views), read through the registry API —
//    display name, the DefaultIcon reference and the open command's executable PATH (never run). No disk crawl,
//    no registry command, nothing written, no default-browser change.
//  - Profiles come from Discover() (documented « User Data » / profiles.ini locations; folder and file metadata).
//  - A logo is read from the browser's own local executable or .ico as a RESOURCE: LoadLibraryEx with
//    LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE maps it as data — no code of it runs.
//    Only an absolute path on a local fixed drive, to a regular file (.exe / .dll / .ico), not a reparse point;
//    never a UNC / network / device path. Anything else, or any failure: the neutral glyph.
// Rows, in order: supported browsers (Comet first, then Chrome, Edge, Brave, Chromium, Firefox), one row per profile
// with data; a supported browser installed without any data, and every installed browser this build cannot read,
// as DISABLED rows saying why; profiles whose browser is no longer registered are importable (« données
// récupérables »); then « Fichier de favoris HTML… ». The logos are decorative (UIA names the row by its text).
#include "browser-import.h"
#include "favicon.h"
#include <cwctype>
#include <memory>
#include <optional>

namespace BrowserImport {
using NativeChrome::FaviconImage;

// One StartMenuInternet registration (synthetic in tests).
struct Registered { std::wstring key, name, command, icon; };

// The executable path of an open command: its first token, quoted or not (« "C:\…\chrome.exe" --single-argument %1 »).
inline std::wstring CommandExe(const std::wstring& command) {
    std::wstring c = AddressBar::Trim(command);
    if (c.empty()) return L"";
    if (c[0] == L'"') { const auto end = c.find(L'"', 1); return end == std::wstring::npos ? L"" : c.substr(1, end - 1); }
    const std::wstring lower = Html::Lower(c);
    const auto exe = lower.find(L".exe");
    if (exe != std::wstring::npos) return c.substr(0, exe + 4);
    const auto space = c.find(L' ');
    return space == std::wstring::npos ? c : c.substr(0, space);
}
inline std::wstring BaseName(const std::wstring& path) { const auto at = path.find_last_of(L"\\/"); return at == std::wstring::npos ? path : path.substr(at + 1); }
// Which supported browser an executable is (by its file name; Chromium's own build also ships chrome.exe).
inline std::optional<Browser> Identify(const std::wstring& exePath) {
    const std::wstring name = Html::Lower(BaseName(exePath)), lower = Html::Lower(exePath);
    if (name == L"chrome.exe") return lower.find(L"\\chromium\\") != std::wstring::npos ? Browser::Chromium : Browser::Chrome;   // i18n-exempt: file names
    if (name == L"msedge.exe") return Browser::Edge;      // i18n-exempt: file name
    if (name == L"brave.exe") return Browser::Brave;      // i18n-exempt: file name
    if (name == L"firefox.exe") return Browser::Firefox;  // i18n-exempt: file name
    if (name == L"comet.exe") return Browser::Comet;      // i18n-exempt: file name
    return std::nullopt;
}
// « C:\…\app.exe,-101 » / « "C:\…\app.ico" » → path and index (negative: a resource id).
inline std::pair<std::wstring, int> IconSpec(const std::wstring& spec) {
    std::wstring s = AddressBar::Trim(spec); int index = 0;
    if (!s.empty() && s[0] == L'"') { const auto end = s.find(L'"', 1); if (end == std::wstring::npos) return {L"", 0}; const std::wstring rest = s.substr(end + 1); s = s.substr(1, end - 1); const auto comma = rest.find(L','); if (comma != std::wstring::npos) index = std::wcstol(rest.c_str() + comma + 1, nullptr, 10); return {s, index}; }
    const auto comma = s.rfind(L',');
    if (comma != std::wstring::npos && comma + 1 < s.size() && s.find_first_not_of(L" -0123456789", comma + 1) == std::wstring::npos) { index = std::wcstol(s.c_str() + comma + 1, nullptr, 10); s = AddressBar::Trim(s.substr(0, comma)); }
    return {s, index};
}
// A logo may be read only from here (review 40dc694): an absolute path on a local fixed drive, inside at least one
// folder, to a regular .exe / .dll / .ico — checked on the OPEN objects, not on names. LogoFile opens every folder of
// the path from the drive's first folder down without following any reparse point (Pin: each must resolve to itself,
// so a junction or symbolic link at any level is refused) and the file itself the same way, and HOLDS them: folders
// without delete sharing (no ancestor can be renamed, deleted or replaced by a junction), the file shared for reading
// only (it cannot be rewritten, renamed or replaced). The loader then opens that very path while everything is held;
// all handles close after it. Anything else: no logo (the neutral glyph), the loader never called.
inline bool LogoPathShape(const std::wstring& path) {
    if (path.size() < 8 || path.size() > 1024 || path[1] != L':' || path[2] != L'\\' || !std::iswalpha(path[0])) return false;   // no UNC, no \\?\, no relative
    if (path.find(L':', 2) != std::wstring::npos || path.find(L"..") != std::wstring::npos || path.find(L'/') != std::wstring::npos || path.find(L"\\\\") != std::wstring::npos) return false;   // no stream, no climbing
    if (path.find(L'\\', 3) == std::wstring::npos) return false;   // directly at the drive root: never a browser's
    const std::wstring ext = Html::Lower(path.substr(path.size() - 4));
    if (ext != L".exe" && ext != L".dll" && ext != L".ico") return false;
    const wchar_t drive[] = {path[0], L':', L'\\', 0};
    return GetDriveTypeW(drive) == DRIVE_FIXED;
}
class LogoFile {
public:
    LogoFile() = default; LogoFile(const LogoFile&) = delete; LogoFile& operator=(const LogoFile&) = delete;
    ~LogoFile() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
    bool Open(const std::wstring& path) {
        if (!LogoPathShape(path)) return false;
        const std::wstring folder = Parent(path), first = path.substr(0, path.find(L'\\', 3));
        if (pin_.Open(first, folder) != Failure::None) return false;
        file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!PlainHandle(file_) || !GetFileInformationByHandle(file_, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))) return false;
        return pin_.Contains(file_, FileName(path));   // the file itself is where the held folders say
    }
private:
    Pin pin_; HANDLE file_ = INVALID_HANDLE_VALUE;
};
inline bool IconPathAllowed(const std::wstring& path) { LogoFile f; return f.Open(path); }
// Tests only: called after the checks, while everything is held, just before the loader opens the path.
inline std::function<void(const std::wstring&)>& LogoHeldForTest() { static std::function<void(const std::wstring&)> hook; return hook; }
// A registered display name, made safe to show: no control characters, bounded.
inline std::wstring CleanName(const std::wstring& n) {
    std::wstring out; for (wchar_t c : n) { if (c < 0x20 || c == 0x7F || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069)) continue; out += c; if (out.size() >= 64) break; }
    return AddressBar::Trim(out);
}

// ---- The rows of the dialog's source list -------------------------------------------------------------------
enum class RowState { Ready, Recoverable, NoData, Unsupported, Html };
struct Row {
    RowState state = RowState::Ready;
    Source source;                     // Ready / Recoverable / Html: what is imported
    std::wstring name;                 // NoData / Unsupported: the browser's name
    std::wstring icon; int iconIndex = 0;   // where its logo is (empty: none known)
    bool Available() const { return state == RowState::Ready || state == RowState::Recoverable || state == RowState::Html; }
};
inline bool SamePathCI(const std::wstring& a, const std::wstring& b) { return !a.empty() && _wcsicmp(a.c_str(), b.c_str()) == 0; }
// `self`: this browser's own executable (never listed as a source).
inline std::vector<Row> Catalog(const std::vector<Registered>& registered, const std::vector<Source>& found, const std::wstring& self) {
    struct Known { std::wstring icon; int index = 0; bool installed = false; };
    std::map<Browser, Known> known; std::vector<Row> unsupported; std::set<std::wstring> names;
    for (const Registered& r : registered) {
        const std::wstring exe = CommandExe(r.command);
        if (exe.empty() || SamePathCI(exe, self)) continue;
        auto [icon, index] = IconSpec(r.icon);
        if (icon.empty()) { icon = exe; index = 0; }
        if (const auto b = Identify(exe)) { Known& k = known[*b]; if (!k.installed) { k = {icon, index, true}; } continue; }
        std::wstring name = CleanName(r.name); if (name.empty()) name = CleanName(BaseName(exe));
        if (name.empty() || !names.insert(Html::Lower(name)).second) continue;
        Row row; row.state = RowState::Unsupported; row.name = name; row.icon = icon; row.iconIndex = index; unsupported.push_back(std::move(row));
    }
    std::vector<Row> rows;
    for (Browser b : {Browser::Comet, Browser::Chrome, Browser::Edge, Browser::Brave, Browser::Chromium, Browser::Firefox}) {
        const auto k = known.find(b); const bool installed = k != known.end();
        bool any = false;
        for (const Source& s : found) {
            if (s.browser != b) continue;
            any = true; Row row; row.state = installed ? RowState::Ready : RowState::Recoverable; row.source = s;
            if (installed) { row.icon = k->second.icon; row.iconIndex = k->second.index; }
            rows.push_back(std::move(row));
        }
        if (!any && installed) { Row row; row.state = RowState::NoData; row.name = BrandName(b); row.source.browser = b; row.icon = k->second.icon; row.iconIndex = k->second.index; rows.push_back(std::move(row)); }
    }
    std::sort(unsupported.begin(), unsupported.end(), [](const Row& a, const Row& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    rows.insert(rows.end(), unsupported.begin(), unsupported.end());
    Row html; html.state = RowState::Html; html.source = HtmlSource(L""); rows.push_back(std::move(html));
    return rows;
}
inline std::wstring RowLabelIn(I18n::Locale l, const Row& r) {
    switch (r.state) {
    case RowState::NoData: case RowState::Unsupported: return r.name;
    default: return LabelIn(l, r.source, r.source.soleProfile);
    }
}
// Second line in the list: why a row is disabled, or that the data outlived its browser.
inline std::wstring RowDetailIn(I18n::Locale l, const Row& r) {
    using I18n::Id;
    switch (r.state) {
    case RowState::Recoverable: return I18n::In(l, Id::import_source_recoverable);
    case RowState::NoData: return I18n::In(l, Id::import_source_no_data);
    case RowState::Unsupported: return I18n::In(l, Id::import_source_unsupported);
    default: return L"";
    }
}
inline std::wstring RowLabel(const Row& r) { return RowLabelIn(I18n::Current(), r); }
inline std::wstring RowDetail(const Row& r) { return RowDetailIn(I18n::Current(), r); }
inline int FirstAvailable(const std::vector<Row>& rows) { for (std::size_t i = 0; i < rows.size(); ++i) if (rows[i].Available()) return static_cast<int>(i); return 0; }

// ---- Reading the registration (production) ------------------------------------------------------------------------
inline std::wstring RegString(HKEY key, const wchar_t* sub) {
    wchar_t buffer[1024]; DWORD size = sizeof(buffer);
    if (RegGetValueW(key, sub, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return L"";
    buffer[std::min<std::size_t>(size / sizeof(wchar_t), 1023)] = 0;
    return buffer;
}
inline std::vector<Registered> ReadStartMenuInternet() {
    std::vector<Registered> out; std::set<std::wstring> seen;
    const struct { HKEY hive; REGSAM view; } places[] = {{HKEY_CURRENT_USER, 0}, {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY}, {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY}};
    for (const auto& p : places) {
        HKEY root = nullptr;
        if (RegOpenKeyExW(p.hive, L"SOFTWARE\\Clients\\StartMenuInternet", 0, KEY_READ | p.view, &root) != ERROR_SUCCESS) continue;
        for (DWORD i = 0; i < 64 && out.size() < 64; ++i) {
            wchar_t name[256]; DWORD length = 256;
            if (RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            HKEY entry = nullptr;
            if (RegOpenKeyExW(root, name, 0, KEY_READ | p.view, &entry) != ERROR_SUCCESS) continue;
            Registered r; r.key = name; r.name = RegString(entry, nullptr); r.icon = RegString(entry, L"DefaultIcon"); r.command = RegString(entry, L"shell\\open\\command");
            RegCloseKey(entry);
            if (seen.insert(Html::Lower(r.key)).second) out.push_back(std::move(r));
        }
        RegCloseKey(root);
    }
    return out;
}
// Tests (--import-sources): « startmenuinternet.txt » in the synthetic folder, one « key|name|command|icon » per line.
inline std::vector<Registered> ReadRegisteredFile(const std::wstring& file) {
    std::vector<Registered> out; std::string bytes;
    if (ReadFileBounded(file, 64 * 1024, bytes) != Failure::None) return out;
    const std::wstring text = Widen(bytes); std::size_t start = 0;
    while (start < text.size() && out.size() < 64) {
        std::size_t end = text.find(L'\n', start); if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start); start = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::vector<std::wstring> f; std::size_t at = 0;
        while (f.size() < 3) { const auto bar = line.find(L'|', at); if (bar == std::wstring::npos) break; f.push_back(line.substr(at, bar - at)); at = bar + 1; }
        if (f.size() != 3) continue;
        out.push_back({f[0], f[1], f[2], line.substr(at)});
    }
    return out;
}

// ---- The logo -----------------------------------------------------------------------------------------------------
// The icon as premultiplied BGRA (what Chrome::DrawSiteImage draws); null when not allowed or not readable.
inline std::shared_ptr<const FaviconImage> IconPixels(HICON icon) {
    if (!icon) return nullptr;
    ICONINFO info{}; if (!GetIconInfo(icon, &info)) return nullptr;
    std::shared_ptr<FaviconImage> out;
    BITMAP bm{};
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmWidth <= 256 && bm.bmHeight > 0 && bm.bmHeight <= 256) {
        const int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4), mask(static_cast<std::size_t>(w) * h * 4);
        HDC dc = GetDC(nullptr);
        const bool color = GetDIBits(dc, info.hbmColor, 0, h, px.data(), &bi, DIB_RGB_COLORS) == h;
        bool alpha = false; for (std::size_t i = 3; i < px.size(); i += 4) if (px[i]) { alpha = true; break; }
        bool masked = !alpha && info.hbmMask && GetDIBits(dc, info.hbmMask, 0, h, mask.data(), &bi, DIB_RGB_COLORS) == h;
        ReleaseDC(nullptr, dc);
        if (color && (alpha || masked)) {
            for (std::size_t i = 0; i < px.size(); i += 4) {
                const unsigned a = alpha ? px[i + 3] : (mask[i] ? 0u : 255u);
                px[i] = static_cast<unsigned char>(px[i] * a / 255); px[i + 1] = static_cast<unsigned char>(px[i + 1] * a / 255); px[i + 2] = static_cast<unsigned char>(px[i + 2] * a / 255); px[i + 3] = static_cast<unsigned char>(a);
            }
            out = std::make_shared<FaviconImage>(); out->width = static_cast<unsigned>(w); out->height = static_cast<unsigned>(h); out->stride = static_cast<unsigned>(w) * 4; out->pixels = std::move(px);
        }
    }
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return out;
}
inline std::shared_ptr<const FaviconImage> LoadLogo(const std::wstring& path, int index, int size = 32) {
    LogoFile held;   // folders and file held until the end of this function (see LogoFile)
    if (!held.Open(path)) return nullptr;
    if (LogoHeldForTest()) LogoHeldForTest()(path);
    HICON icon = nullptr;
    if (Html::Lower(path.substr(path.size() - 4)) == L".ico") {
        icon = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE));
    } else {
        HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);   // data only: nothing runs
        if (!m) return nullptr;
        LPCWSTR group = nullptr; std::vector<std::wstring> names;
        if (index < 0) group = MAKEINTRESOURCEW(static_cast<WORD>(-index));
        else {
            struct Find { int want, at = 0; LPCWSTR found = nullptr; std::wstring name; } f{index};
            EnumResourceNamesW(m, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */, [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR p) -> BOOL {
                auto* f = reinterpret_cast<Find*>(p);
                if (f->at++ != f->want) return TRUE;
                if (IS_INTRESOURCE(name)) f->found = name; else { f->name = name; f->found = f->name.c_str(); }
                return FALSE;
            }, reinterpret_cast<LONG_PTR>(&f));
            if (f.found && !IS_INTRESOURCE(f.found)) { names.push_back(f.name); group = names.back().c_str(); } else group = f.found;
        }
        if (group) {
            HRSRC dir = FindResourceW(m, group, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */); HGLOBAL dirData = dir ? LoadResource(m, dir) : nullptr;
            auto* bits = dirData ? static_cast<PBYTE>(LockResource(dirData)) : nullptr;
            const int id = bits ? LookupIconIdFromDirectoryEx(bits, TRUE, size, size, LR_DEFAULTCOLOR) : 0;
            HRSRC one = id ? FindResourceW(m, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(3) /* RT_ICON */) : nullptr; HGLOBAL oneData = one ? LoadResource(m, one) : nullptr;
            auto* iconBits = oneData ? static_cast<PBYTE>(LockResource(oneData)) : nullptr;
            const DWORD length = one ? SizeofResource(m, one) : 0;
            if (iconBits && length) icon = CreateIconFromResourceEx(iconBits, length, TRUE, 0x00030000, size, size, LR_DEFAULTCOLOR);
        }
        FreeLibrary(m);
    }
    auto pixels = IconPixels(icon);
    if (icon) DestroyIcon(icon);
    return pixels;
}

} // namespace BrowserImport
