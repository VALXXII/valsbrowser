// Fake `codex.exe` for agent_connect_test (never the real CLI). Placed in a temporary PATH folder.
//   codex mcp add <name> -- <command> [args...]   appends [mcp_servers.<name>] like codex-cli 0.166.0
//                                                 (TOML literal strings), replacing an existing table
//   codex mcp remove <name>                        removes that table
// Every call appends "cwd=<cwd>|CODEX_HOME=<value>|<argv...>" to %CODEX_HOME%\fake-codex.log.
// %CODEX_HOME%\fake-mode: "fail" => exit 3 without changes; "noop" => exit 0 without changes.
#include <windows.h>

#include <string>
#include <vector>

namespace {
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::string ReadAll(const std::wstring& path) {
    std::string s;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return s;
    char b[65536];
    DWORD got = 0;
    while (ReadFile(f, b, sizeof b, &got, nullptr) && got) s.append(b, got);
    CloseHandle(f);
    return s;
}
void WriteAll(const std::wstring& path, const std::string& s, bool append) {
    HANDLE f = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, 0, nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD put = 0;
    WriteFile(f, s.data(), DWORD(s.size()), &put, nullptr);
    CloseHandle(f);
}
// Like toml_edit in codex-cli: literal string when possible, else a basic string.
std::string TomlStr(const std::string& s) {
    if (s.find('\'') == std::string::npos) return "'" + s + "'";
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}
// Removes the "[mcp_servers.<name>]" table (header line up to the next header line).
std::string RemoveTable(const std::string& text, const std::string& name) {
    const std::string header = "[mcp_servers." + name + "]";
    std::string out;
    bool skipping = false;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i);
        e = e == std::string::npos ? text.size() : e + 1;
        const std::string line = text.substr(i, e - i);
        if (line.compare(0, header.size(), header) == 0) skipping = true;
        else if (!line.empty() && line[0] == '[') skipping = false;
        if (!skipping) out += line;
        i = e;
    }
    return out;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    wchar_t home[2048] = L"";
    const DWORD n = GetEnvironmentVariableW(L"CODEX_HOME", home, 2048);
    if (!n || n >= 2048) return 9;
    const std::wstring h = home;
    wchar_t cwd[2048] = L"";
    GetCurrentDirectoryW(2048, cwd);
    std::string log = "cwd=" + Narrow(cwd) + "|CODEX_HOME=" + Narrow(h);
    for (int i = 1; i < argc; ++i) log += "|" + Narrow(argv[i]);
    WriteAll(h + L"\\fake-codex.log", log + "\n", true);
    const std::string mode = ReadAll(h + L"\\fake-mode");
    if (mode.compare(0, 4, "fail") == 0) return 3;
    if (mode.compare(0, 4, "noop") == 0) return 0;
    if (argc < 4 || std::wstring(argv[1]) != L"mcp") return 2;
    const std::wstring config = h + L"\\config.toml";
    const std::string name = Narrow(argv[3]);
    std::string text = RemoveTable(ReadAll(config), name);
    if (std::wstring(argv[2]) == L"add") {
        if (argc < 6 || std::wstring(argv[4]) != L"--") return 2;
        if (!text.empty() && text.back() != '\n') text += "\n";
        if (!text.empty()) text += "\n";
        text += "[mcp_servers." + name + "]\ncommand = " + TomlStr(Narrow(argv[5])) + "\n";
        if (argc > 6) {
            text += "args = [";
            for (int i = 6; i < argc; ++i) text += std::string(i > 6 ? ", " : "") + TomlStr(Narrow(argv[i]));
            text += "]\n";
        }
    } else if (std::wstring(argv[2]) != L"remove") {
        return 2;
    }
    WriteAll(config, text, false);
    return 0;
}
