#pragma once
// Embedded payload of browser-<version>-win-x64-setup.exe (design/windows-setup-plan.md).
//
//   setup.exe = setup stub (PE image) | browser-<version>-win-x64.lsrupd (byte-identical) | 128-byte trailer
//
// The overlay is everything after the last section's raw data, up to the Authenticode certificate table when the file
// is signed (so a later signature does not move the trailer), else up to EOF. The trailer is the last TrailerSize bytes
// of the overlay; tools/release/package.py writes and verifies the same layout (write_setup / verify_setup).
//
// Nothing here parses the package itself: the copied-out .lsrupd goes to UpdatePackage::ReadPackageIndex /
// ExtractPackage, the validator every client already runs on downloaded updates. The trailer SHA-256 detects a
// truncated or corrupted download; it is not a signature (provenance = HTTPS release + published SHA256SUMS).
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "update-package.h"

namespace SetupPayload {

constexpr std::size_t TrailerSize = 128;
constexpr char Magic[16] = {'B', 'R', 'O', 'W', 'S', 'E', 'R', '-', 'S', 'E', 'T', 'U', 'P', ' ', '1', 0};
constexpr char EndMagic[8] = {'B', 'R', 'S', 'E', 'T', 'U', 'P', '!'};
constexpr std::size_t HeaderProbe = 4096;  // PE headers + section table must fit

struct Overlay {
    std::uint64_t start = 0, end = 0, fileSize = 0;
    std::uint32_t securityDirOffset = 0;  // file offset of IMAGE_DIRECTORY_ENTRY_SECURITY in the optional header
    bool signedFile = false;
};

struct Layout {
    std::string version;
    std::uint64_t packageOffset = 0, packageSize = 0;
    UpdatePackage::Digest sha256{};
    Overlay overlay;
};

inline std::uint16_t U16(const std::uint8_t* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
inline std::uint32_t U32(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
inline std::uint64_t U64(const std::uint8_t* p) { return std::uint64_t(U32(p)) | (std::uint64_t(U32(p + 4)) << 32); }

// Pure: PE32+ x64 headers (first bytes of the file) -> overlay bounds.
inline bool LocateOverlay(const std::uint8_t* head, std::size_t headSize, std::uint64_t fileSize, Overlay& out, std::string& error) {
    out = Overlay{};
    out.fileSize = fileSize;
    if (headSize < 0x40 || head[0] != 'M' || head[1] != 'Z') return error = "not a PE image", false;
    const std::uint32_t pe = U32(head + 0x3C);
    if (pe < 0x40 || std::uint64_t(pe) + 24 > headSize || std::memcmp(head + pe, "PE\0\0", 4) != 0) return error = "bad PE header", false;
    if (U16(head + pe + 4) != 0x8664) return error = "not x64", false;
    const std::uint32_t sections = U16(head + pe + 6), optSize = U16(head + pe + 20);
    const std::uint32_t opt = pe + 24;
    if (optSize < 112 + 8 * 5 || std::uint64_t(opt) + optSize > headSize || U16(head + opt) != 0x20B) return error = "not PE32+", false;
    if (sections == 0 || sections > 96 || std::uint64_t(opt) + optSize + 40ull * sections > headSize) return error = "bad section table", false;
    std::uint64_t end = U32(head + opt + 60);  // SizeOfHeaders
    for (std::uint32_t i = 0; i < sections; ++i) {
        const std::uint8_t* s = head + opt + optSize + 40 * i;
        const std::uint64_t raw = U32(s + 16), ptr = U32(s + 20);
        if (raw && ptr + raw > end) end = ptr + raw;
    }
    if (end > fileSize) return error = "image larger than file", false;
    const std::uint32_t dirs = U32(head + opt + 108);  // NumberOfRvaAndSizes
    out.securityDirOffset = opt + 112 + 8 * 4;
    std::uint64_t overlayEnd = fileSize;
    if (dirs > 4) {
        const std::uint64_t certOffset = U32(head + opt + 112 + 8 * 4), certSize = U32(head + opt + 112 + 8 * 4 + 4);
        if (certOffset || certSize) {
            // A certificate table must follow the overlay and run exactly to EOF.
            if (certOffset < end || certOffset + certSize != fileSize) return error = "bad certificate table", false;
            overlayEnd = certOffset;
            out.signedFile = true;
        }
    }
    out.start = end;
    out.end = overlayEnd;
    return true;
}

// Pure: validates the trailer against the overlay and the version compiled into this setup.
inline bool ParseTrailer(const std::uint8_t* t, const Overlay& overlay, const std::string& expectedVersion, Layout& out, std::string& error) {
    out = Layout{};
    out.overlay = overlay;
    if (overlay.end < overlay.start || overlay.end - overlay.start < TrailerSize) return error = "no embedded package", false;
    if (std::memcmp(t, Magic, 16) != 0 || std::memcmp(t + 120, EndMagic, 8) != 0) return error = "bad trailer magic", false;
    const char* v = reinterpret_cast<const char*>(t + 16);
    std::size_t len = 0;
    while (len < 32 && v[len]) ++len;
    for (std::size_t i = len; i < 32; ++i)
        if (v[i]) return error = "bad trailer version padding", false;
    out.version.assign(v, len);
    UpdatePackage::Version parsed;
    if (!UpdatePackage::ParseVersion(out.version, parsed) || UpdatePackage::IsZero(parsed)) return error = "bad trailer version", false;
    if (out.version != expectedVersion) return error = "trailer version " + out.version + " != setup " + expectedVersion, false;
    out.packageOffset = U64(t + 48);
    out.packageSize = U64(t + 56);
    std::memcpy(out.sha256.data(), t + 64, 32);
    for (int i = 96; i < 120; ++i)
        if (t[i]) return error = "bad trailer padding", false;
    if (out.packageOffset != overlay.start) return error = "package offset does not start the overlay", false;
    if (out.packageSize == 0 || out.packageSize > UpdatePackage::MaxPackageBytes) return error = "bad package size", false;
    if (out.packageSize != overlay.end - overlay.start - TrailerSize) return error = "package size does not fill the overlay", false;
    return true;
}

inline bool ReadAt(HANDLE f, std::uint64_t offset, void* buffer, DWORD size) {
    OVERLAPPED o{};
    o.Offset = DWORD(offset);
    o.OffsetHigh = DWORD(offset >> 32);
    DWORD got = 0;
    return ReadFile(f, buffer, size, &got, &o) && got == size;
}

// Opens `setup` and validates headers + trailer (no hashing of the package yet: see CopyPackage).
inline bool Open(const std::wstring& setup, const std::string& expectedVersion, Layout& out, std::string& error) {
    HANDLE f = CreateFileW(setup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return error = "cannot open setup file", false;
    LARGE_INTEGER size{};
    std::vector<std::uint8_t> head(HeaderProbe);
    std::uint8_t trailer[TrailerSize];
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0;
    const std::uint64_t fileSize = ok ? std::uint64_t(size.QuadPart) : 0;
    const DWORD headSize = DWORD(fileSize < HeaderProbe ? fileSize : HeaderProbe);
    Overlay overlay;
    ok = ok && ReadAt(f, 0, head.data(), headSize);
    if (!ok) error = "cannot read setup file";
    ok = ok && LocateOverlay(head.data(), headSize, fileSize, overlay, error);
    if (ok && overlay.end - overlay.start < TrailerSize) ok = (error = "no embedded package", false);
    if (ok && !ReadAt(f, overlay.end - TrailerSize, trailer, TrailerSize)) ok = (error = "cannot read trailer", false);
    ok = ok && ParseTrailer(trailer, overlay, expectedVersion, out, error);
    CloseHandle(f);
    return ok;
}

// Copies the embedded package to `dest` (created with CREATE_NEW, never replacing anything), hashing on the fly. On any
// failure (read, write, disk full, hash mismatch, cancel) `dest` is deleted. progress(done, total) is called per chunk.
inline bool CopyPackage(const std::wstring& setup, const Layout& layout, const std::wstring& dest, const std::atomic<bool>* cancel,
                        const std::function<void(std::uint64_t, std::uint64_t)>& progress, std::string& error) {
    HANDLE in = CreateFileW(setup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (in == INVALID_HANDLE_VALUE) return error = "cannot open setup file", false;
    HANDLE out = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        CloseHandle(in);
        return error = "cannot create " + UpdatePackage::Narrow(dest) + " (error " + std::to_string(GetLastError()) + ")", false;
    }
    UpdatePackage::Sha256 hash;
    std::vector<std::uint8_t> buffer(1 << 20);
    std::uint64_t done = 0;
    bool ok = hash.Ok();
    while (ok && done < layout.packageSize) {
        if (cancel && cancel->load()) {
            ok = false;
            error = "cancelled";
            break;
        }
        const DWORD want = DWORD(std::min<std::uint64_t>(buffer.size(), layout.packageSize - done));
        DWORD put = 0;
        if (!ReadAt(in, layout.packageOffset + done, buffer.data(), want)) {
            ok = false;
            error = "read failed";
            break;
        }
        if (!WriteFile(out, buffer.data(), want, &put, nullptr) || put != want) {
            ok = false;
            error = "write failed (error " + std::to_string(GetLastError()) + ")";
            break;
        }
        hash.Update(buffer.data(), want);
        done += want;
        if (progress) progress(done, layout.packageSize);
    }
    if (ok && !FlushFileBuffers(out)) ok = (error = "flush failed", false);
    if (ok && hash.Final() != layout.sha256) ok = (error = "embedded package hash mismatch", false);
    CloseHandle(out);
    CloseHandle(in);
    if (!ok) DeleteFileW(dest.c_str());
    return ok;
}

// The setup image without its payload (the stub), with the certificate directory cleared: <root>\uninstall.exe.
inline bool WriteStub(const std::wstring& setup, const Layout& layout, const std::wstring& dest, std::string& error) {
    HANDLE in = CreateFileW(setup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE) return error = "cannot open setup file", false;
    std::vector<std::uint8_t> image(std::size_t(layout.overlay.start));
    const bool read = layout.overlay.start <= (64ull << 20) && ReadAt(in, 0, image.data(), DWORD(image.size()));
    CloseHandle(in);
    if (!read) return error = "cannot read setup image", false;
    if (layout.overlay.securityDirOffset + 8 <= image.size()) std::memset(image.data() + layout.overlay.securityDirOffset, 0, 8);
    HANDLE out = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (out == INVALID_HANDLE_VALUE) return error = "cannot create uninstaller", false;
    DWORD put = 0;
    const bool ok = WriteFile(out, image.data(), DWORD(image.size()), &put, nullptr) && put == image.size() && FlushFileBuffers(out);
    CloseHandle(out);
    if (!ok) {
        DeleteFileW(dest.c_str());
        return error = "cannot write uninstaller", false;
    }
    return true;
}

}  // namespace SetupPayload
