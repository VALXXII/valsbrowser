// Stand-in for MicrosoftEdgeWebview2Setup.exe in install_test (NOT the Microsoft installer: it
// installs nothing). Records how it was started into %FAKE_SETUP_OUT%, then exits with
// %FAKE_SETUP_EXIT% after %FAKE_SETUP_SLEEP_MS%.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

static std::wstring Env(const wchar_t* name) {
    wchar_t buffer[2048]; DWORD n = GetEnvironmentVariableW(name, buffer, 2048);
    return n && n < 2048 ? std::wstring(buffer, n) : std::wstring();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    // The browser keeps the downloaded file open deny-write/deny-delete while it runs.
    HANDLE w = CreateFileW(self, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    const bool writable = w != INVALID_HANDLE_VALUE;
    if (writable) CloseHandle(w);
    const std::wstring out = Env(L"FAKE_SETUP_OUT");
    if (!out.empty()) {
        wchar_t text[4096];
        swprintf(text, 4096, L"cmd=%ls\nconsole=%d\nselfWritable=%d\n", GetCommandLineW(), GetConsoleWindow() != nullptr, writable ? 1 : 0);
        char utf8[8192]; const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
        HANDLE f = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) { DWORD written = 0; if (n > 1) WriteFile(f, utf8, DWORD(n - 1), &written, nullptr); CloseHandle(f); }
    }
    const std::wstring sleep = Env(L"FAKE_SETUP_SLEEP_MS");
    if (!sleep.empty()) Sleep(static_cast<DWORD>(_wtoi(sleep.c_str())));
    const std::wstring code = Env(L"FAKE_SETUP_EXIT");
    return code.empty() ? 0 : static_cast<int>(wcstoul(code.c_str(), nullptr, 0));
}
