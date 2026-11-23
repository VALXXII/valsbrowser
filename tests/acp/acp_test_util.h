#pragma once
// Shared helpers for the ACP client and agent runtime tests (temporary folders under build\acp-test\tmp only).
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace AcpTest {

inline int& Failures() {
    static int f = 0;
    return f;
}
inline int& Checks() {
    static int c = 0;
    return c;
}
#define CHECK(cond)                                                     \
    do {                                                                \
        ++AcpTest::Checks();                                            \
        if (!(cond)) {                                                  \
            ++AcpTest::Failures();                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::fflush(stdout);                                        \
        }                                                               \
    } while (0)

inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
inline std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
inline std::wstring FullPath(const std::wstring& p) {
    wchar_t b[4096];
    const DWORD n = GetFullPathNameW(p.c_str(), 4096, b, nullptr);
    return std::wstring(b, n);
}
inline bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
inline bool MakeDirs(const std::wstring& path) {
    for (std::size_t i = 3; i <= path.size(); ++i)
        if (i == path.size() || path[i] == L'\\') {
            const std::wstring part = path.substr(0, i);
            if (!CreateDirectoryW(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
    return true;
}
inline bool RemoveTree(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        return (a & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(path.c_str()) != 0 : DeleteFileW(path.c_str()) != 0;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name != L"." && name != L"..") RemoveTree(path + L"\\" + name);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(path.c_str()) != 0;
}
inline std::string ReadFileText(const std::wstring& path) {
    std::string s;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return s;
    char b[65536];
    DWORD got = 0;
    while (ReadFile(f, b, sizeof b, &got, nullptr) && got) s.append(b, got);
    CloseHandle(f);
    return s;
}
inline bool WriteFileText(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(f, data.data(), DWORD(data.size()), &put, nullptr) && put == data.size();
    CloseHandle(f);
    return ok;
}
inline bool ProcessAlive(unsigned long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}
inline bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

}  // namespace AcpTest
