// Prints the taskbar identity Windows reads from the main window of process <pid>:
//   aumid=<...>\nrelaunch=<...>\nname=<...>\nicon=<...>
// Read-only (SHGetPropertyStoreForWindow + GetValue). Used by tests/e2e/taskbar_identity_e2e.py.
#include <windows.h>
#include <cstdio>
#include <string>
#include "../../src/taskbar-identity.h"

static DWORD target = 0; static HWND found = nullptr;
static BOOL CALLBACK Find(HWND h, LPARAM) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid); wchar_t cls[64]{};
    if (pid == target && GetClassNameW(h, cls, 64) && std::wstring(cls) == L"LisiereBrowser") { found = h; return FALSE; }
    return TRUE;
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    target = static_cast<DWORD>(_wtoi(argv[1]));
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EnumWindows(Find, 0);
    if (!found) { std::printf("window=none\n"); return 3; }
    auto out = [](const char* k, const std::wstring& v) { char b[2048]{}; WideCharToMultiByte(CP_UTF8, 0, v.c_str(), -1, b, sizeof b, nullptr, nullptr); std::printf("%s=%s\n", k, b); };
    out("aumid", TaskbarIdentity::WindowValue(found, PKEY_AppUserModel_ID));
    out("relaunch", TaskbarIdentity::WindowValue(found, PKEY_AppUserModel_RelaunchCommand));
    out("name", TaskbarIdentity::WindowValue(found, PKEY_AppUserModel_RelaunchDisplayNameResource));
    out("icon", TaskbarIdentity::WindowValue(found, PKEY_AppUserModel_RelaunchIconResource));
    return 0;
}
