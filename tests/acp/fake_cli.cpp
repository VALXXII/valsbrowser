// Fake `claude.exe` / `codex.exe` for the agent runtime tests (never the real CLI). Copied under temporary folders.
// Behaviour comes from files next to the executable:
//   <exe>.version   text printed by `--version` ("2.1.325 (Claude Code)"); without it: exit 1, nothing printed
//   <exe>.login     "0" or "1": exit code of `auth status` / `login status`
// Every call appends its arguments to <exe>.calls.
#include <windows.h>

#include <string>

namespace {
std::string Read(const std::wstring& path) {
    std::string s;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return s;
    char b[4096];
    DWORD got = 0;
    while (ReadFile(f, b, sizeof b, &got, nullptr) && got) s.append(b, got);
    CloseHandle(f);
    return s;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    wchar_t self[4096];
    const DWORD n = GetModuleFileNameW(nullptr, self, 4096);
    const std::wstring exe(self, n);
    std::string line;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        const int size = WideCharToMultiByte(CP_UTF8, 0, a.data(), int(a.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(std::size_t(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, a.data(), int(a.size()), utf8.data(), size, nullptr, nullptr);
        line += (i > 1 ? " " : "") + utf8;
    }
    line += "\n";
    HANDLE calls = CreateFileW((exe + L".calls").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (calls != INVALID_HANDLE_VALUE) {
        DWORD put = 0;
        WriteFile(calls, line.data(), DWORD(line.size()), &put, nullptr);
        CloseHandle(calls);
    }
    const std::wstring first = argc > 1 ? argv[1] : L"", second = argc > 2 ? argv[2] : L"";
    if (first == L"--version") {
        const std::string version = Read(exe + L".version");
        if (version.empty()) return 1;
        if (version.rfind("hang", 0) == 0) Sleep(60000);  // a command that never answers
        DWORD put = 0;
        // A chatty CLI: more than a pipe buffer of output must not block the caller.
        const std::string text = version + "\n" + std::string(first == L"--version" && version.find("chatty") != std::string::npos ? 200000 : 0, '.');
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), DWORD(text.size()), &put, nullptr);
        return 0;
    }
    if ((first == L"auth" || first == L"login") && second == L"status") {
        const std::string login = Read(exe + L".login");
        DWORD put = 0;
        const std::string secret = "{\"loggedIn\":true,\"email\":\"someone@example.test\"}\n";  // must never be read by the browser
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), secret.data(), DWORD(secret.size()), &put, nullptr);
        return login.empty() ? 2 : login[0] - '0';
    }
    // « Ton agent » trials (design/ton-agent-interface.md): the sign-in (`claude auth login`, `codex login`) succeeds at once
    // and is remembered in <exe>.login, as the real CLI would after the web page.
    if ((first == L"auth" && second == L"login") || (first == L"login" && second != L"status")) {
        HANDLE f = CreateFileW((exe + L".login").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) { DWORD put = 0; WriteFile(f, "0", 1, &put, nullptr); CloseHandle(f); }
        const std::string done = "Signed in (fake CLI).\n";
        DWORD put = 0; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), done.data(), DWORD(done.size()), &put, nullptr);
        return 0;
    }
    if (first == L"hang") Sleep(60000);
    return 0;
}
