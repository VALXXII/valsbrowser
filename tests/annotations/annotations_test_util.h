#pragma once
// Shared helpers for the annotation store tests (isolated temp roots under build\annotations-test\tmp).
#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/annotations.h"

namespace AnnTest {

inline int& Failures() {
    static int f = 0;
    return f;
}
inline int& Checks() {
    static int c = 0;
    return c;
}
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++AnnTest::Checks();                                              \
        if (!(cond)) {                                                    \
            ++AnnTest::Failures();                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            std::fflush(stdout);                                          \
        }                                                                 \
    } while (0)

inline std::wstring FullPath(const std::wstring& p) {
    wchar_t b[4096];
    DWORD n = GetFullPathNameW(p.c_str(), 4096, b, nullptr);
    return std::wstring(b, n);
}
inline bool RemoveTree(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(path.c_str()) != 0;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return RemoveDirectoryW(path.c_str()) != 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n != L"." && n != L"..") RemoveTree(path + L"\\" + n);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    return RemoveDirectoryW(path.c_str()) != 0;
}
// Fresh isolated directory (never the real %LOCALAPPDATA%).
inline std::wstring TempDir(const std::wstring& name) {
    CreateDirectoryW(FullPath(L"build").c_str(), nullptr);
    CreateDirectoryW(FullPath(L"build\\annotations-test").c_str(), nullptr);
    const std::wstring base = FullPath(L"build\\annotations-test\\tmp");
    CreateDirectoryW(base.c_str(), nullptr);
    const std::wstring dir = base + L"\\" + name;
    RemoveTree(dir);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
inline bool WriteText(const std::wstring& path, const std::string& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(f, text.data(), DWORD(text.size()), &put, nullptr) && put == text.size();
    CloseHandle(f);
    return ok;
}
inline std::string ReadText(const std::wstring& path) {
    std::string s;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return s;
    char b[65536];
    DWORD got = 0;
    while (ReadFile(f, b, sizeof b, &got, nullptr) && got) s.append(b, got);
    CloseHandle(f);
    return s;
}
inline bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Directory junction without admin rights (mount point reparse data).
inline bool CreateJunction(const std::wstring& link, const std::wstring& target) {
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const std::wstring sub = L"\\??\\" + target, print = target;
    const std::size_t subBytes = sub.size() * 2, printBytes = print.size() * 2;
    const std::size_t pathBytes = subBytes + 2 + printBytes + 2;
    std::string buffer(8 + 8 + pathBytes, '\0');
    auto* p = reinterpret_cast<unsigned char*>(buffer.data());
    auto put16 = [&](std::size_t off, std::uint16_t v) { std::memcpy(p + off, &v, 2); };
    const std::uint32_t tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(p, &tag, 4);
    put16(4, std::uint16_t(8 + pathBytes));
    put16(8, 0);
    put16(10, std::uint16_t(subBytes));
    put16(12, std::uint16_t(subBytes + 2));
    put16(14, std::uint16_t(printBytes));
    std::memcpy(p + 16, sub.data(), subBytes);
    std::memcpy(p + 16 + subBytes + 2, print.data(), printBytes);
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, p, DWORD(buffer.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(h);
    return ok != 0;
}

// Smallest valid PNG header for a w x h image (IHDR is what the store inspects; the IDAT is a fixed 1x1 body
// that viewers may reject for other sizes, which is irrelevant to the store).
inline std::string TinyPng(std::uint32_t w = 1, std::uint32_t h = 1) {
    static const unsigned char body[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
        0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    std::string s(reinterpret_cast<const char*>(body), sizeof body);
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        s[at] = char(v >> 24);
        s[at + 1] = char(v >> 16);
        s[at + 2] = char(v >> 8);
        s[at + 3] = char(v);
    };
    put32(16, w);
    put32(20, h);
    return s;
}

inline BrowserAnnotations::Annotation Sample(const std::string& project, const std::string& note = "Le bouton Payer") {
    BrowserAnnotations::Annotation a;
    a.project = project;
    a.note = note;
    a.source.channel = "preview";
    a.source.version = "0.3.120";
    a.source.url = "http://localhost:5173/checkout?step=2";
    a.source.title = "Paiement";
    a.target.selector = "main form.checkout > button.btn-primary";
    a.target.selectorUnique = true;
    a.target.path = {"html", "body", "main", "form.checkout", "button.btn-primary"};
    a.target.tag = "button";
    a.target.role = "button";
    a.target.accessibleName = "Payer";
    a.target.text = "Payer";
    a.target.rect = {412, 688, 120, 40};
    a.target.viewport = {1280, 760, 0, 420, 1.5, 1};
    a.target.styles = {{"display", "inline-flex"}, {"color", "rgb(255, 255, 255)"}};
    a.target.attributes = {{"type", "submit"}, {"data-testid", "pay-button"}};
    return a;
}

inline int Finish(const char* suite) {
    std::printf("%s: %d checks, %d failures\n", suite, Checks(), Failures());
    return Failures() ? 1 : 0;
}

}  // namespace AnnTest
