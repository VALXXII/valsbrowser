#pragma once
#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/update-package.h"

namespace UpdateTest {

inline int& Failures() {
    static int f = 0;
    return f;
}
inline int& Checks() {
    static int c = 0;
    return c;
}
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++UpdateTest::Checks();                                                     \
        if (!(cond)) {                                                              \
            ++UpdateTest::Failures();                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            std::fflush(stdout);                                                    \
        }                                                                           \
    } while (0)

inline std::wstring FullPath(const std::wstring& p) {
    wchar_t b[4096];
    DWORD n = GetFullPathNameW(p.c_str(), 4096, b, nullptr);
    return std::wstring(b, n);
}
// Fresh isolated directory under build\update-core\tmp (never the real %LOCALAPPDATA%).
inline std::wstring TempDir(const std::wstring& name) {
    std::wstring base = FullPath(L"build\\update-core\\tmp");
    CreateDirectoryW(FullPath(L"build\\update-core").c_str(), nullptr);
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring dir = base + L"\\" + name;
    UpdatePackage::RemoveTree(dir);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
inline bool WriteText(const std::wstring& path, const std::string& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(f, text.data(), DWORD(text.size()), &put, nullptr) && put == text.size();
    CloseHandle(f);
    return ok;
}
inline std::string ReadText(const std::wstring& path) {
    std::string s;
    UpdatePackage::ReadSmallFile(path, 64 * 1024 * 1024, s);
    return s;
}
inline std::string ReadBinary(const std::wstring& path) { return ReadText(path); }
inline std::string HashOf(const std::wstring& path) {
    UpdatePackage::Digest d;
    std::uint64_t size = 0;
    if (!UpdatePackage::Sha256File(path, d, size)) return "missing";
    return UpdatePackage::Hex(d);
}

// Directory junction without admin rights (mount point reparse data).
inline bool CreateJunction(const std::wstring& link, const std::wstring& target) {
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::wstring sub = L"\\??\\" + target, print = target;
    std::size_t subBytes = sub.size() * 2, printBytes = print.size() * 2;
    std::size_t pathBytes = subBytes + 2 + printBytes + 2;
    std::string buffer(8 + 8 + pathBytes, '\0');
    auto* p = reinterpret_cast<unsigned char*>(buffer.data());
    auto put16 = [&](std::size_t off, std::uint16_t v) { std::memcpy(p + off, &v, 2); };
    std::uint32_t tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(p, &tag, 4);
    put16(4, std::uint16_t(8 + pathBytes));
    put16(8, 0);
    put16(10, std::uint16_t(subBytes));
    put16(12, std::uint16_t(subBytes + 2));
    put16(14, std::uint16_t(printBytes));
    std::memcpy(p + 16, sub.data(), subBytes);
    std::memcpy(p + 16 + subBytes + 2, print.data(), printBytes);
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, p, DWORD(buffer.size()), nullptr, 0, &ret, nullptr);
    CloseHandle(h);
    return ok != 0;
}

inline int Report(const char* name) {
    std::printf("%s: %d checks, %d failures\n", name, Checks(), Failures());
    return Failures() ? 1 : 0;
}

}  // namespace UpdateTest
