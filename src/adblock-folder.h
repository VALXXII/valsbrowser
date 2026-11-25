#pragma once
// Where the bundled ad blocker (uBlock Origin Lite, assets\extensions\ublock-lite) is installed from.
//
// The engine derives an unpacked extension's id from its folder. Installing straight from
// versions\<v>\assets\... gave it a new id after every update: the first launch found « a previous
// install », removed it and installed again while the restored tabs were loading, which aborted them
// (WebErrorStatus=9, « La connexion a été interrompue »). The extension is therefore copied once per
// uBlock version to <data root>\extensions\ublock-lite-<version>, a folder no update moves or prunes:
// same uBlock version, same folder, same id, nothing to reinstall.
//
// A copy is published by renaming a complete temporary folder, so a half-written folder never carries
// the final name; a final folder without its manifest is never used. Any failure, a missing data root
// or a version that is not plain digits and dots falls back to the bundled folder (the former behaviour).
//
// The browser runs Stable() on a background thread (main.cpp, AdBlockSourceStart): a browser closed during
// that copy leaves its temporary folder, removed by the next copy once the process that wrote it is gone.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace AdBlockFolder {

// The manifest's "version" ("2026.1103.1743"), or "" when absent or not made of digits and dots only.
inline std::wstring ManifestVersion(const std::wstring& folder) {
    std::ifstream in(std::filesystem::path(folder) / "manifest.json", std::ios::binary);
    if (!in) return L"";
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t key = text.find("\"version\"");
    if (key == std::string::npos) return L"";
    size_t at = text.find(':', key), open = at == std::string::npos ? at : text.find('"', at);
    if (open == std::string::npos) return L"";
    const size_t close = text.find('"', open + 1);
    if (close == std::string::npos || close == open + 1 || close - open > 33) return L"";
    std::wstring version;
    for (size_t i = open + 1; i < close; ++i) {
        const char c = text[i];
        if (!((c >= '0' && c <= '9') || c == '.')) return L"";
        version += static_cast<wchar_t>(c);
    }
    return version.front() == L'.' || version.back() == L'.' ? L"" : version;
}

// A process that still runs (or that exists but cannot be opened by us): its copy may be in progress.
inline bool ProcessAlive(unsigned long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

// Removes the temporary folders of copies that never finished (« ublock-lite-<v>.tmp-<pid> » whose process is
// gone): up to 47 MB each. Another window's copy in progress (its process runs) is left alone. Returns how many.
inline int RemoveAbandoned(const std::filesystem::path& parent) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> abandoned;
    for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        const size_t at = name.rfind(L".tmp-");
        if (name.rfind(L"ublock-lite-", 0) != 0 || at == std::wstring::npos) continue;
        const std::wstring digits = name.substr(at + 5);
        if (digits.empty() || digits.size() > 10 || digits.find_first_not_of(L"0123456789") != std::wstring::npos) continue;
        const unsigned long long pid = std::stoull(digits);
        if (pid > 0xFFFFFFFFull || pid == GetCurrentProcessId() || ProcessAlive(static_cast<unsigned long>(pid))) continue;
        abandoned.push_back(it->path());
    }
    int removed = 0;
    for (const auto& folder : abandoned) {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
        if (!fs::exists(folder, ignored)) ++removed;
    }
    return removed;
}

// The folder Stable() installs from when all goes well, without copying anything: <root>\extensions\ublock-lite-<version>,
// or the bundled folder when that is not possible (no data root, no extension, no usable version). The browser compares
// it with the folder of its last install before the first tab loads: another folder means the install is replaced
// (main.cpp, AdBlockRemovalExpected).
inline std::wstring Expected(const std::wstring& bundled, const std::wstring& root) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (root.empty() || !fs::is_regular_file(fs::path(bundled) / "manifest.json", ec)) return bundled;
    const std::wstring version = ManifestVersion(bundled);
    if (version.empty()) return bundled;
    return (fs::path(root) / L"extensions" / (L"ublock-lite-" + version)).wstring();
}

// i18n-exempt-begin: `note` is a log line (SETTINGS adblock source), never shown in the interface
// The folder to install from: <root>\extensions\ublock-lite-<version>, copied from `bundled` when
// missing; `bundled` itself when that is not possible. `note` says what happened (for the log).
inline std::wstring Stable(const std::wstring& bundled, const std::wstring& root, std::wstring* note = nullptr) {
    namespace fs = std::filesystem;
    auto say = [&](const std::wstring& s) { if (note) *note = s; };
    std::error_code ec;
    if (root.empty() || !fs::is_regular_file(fs::path(bundled) / "manifest.json", ec)) { say(L"bundled folder (no data root or no extension)"); return bundled; }
    const std::wstring version = ManifestVersion(bundled);
    if (version.empty()) { say(L"bundled folder (manifest version not usable)"); return bundled; }
    const fs::path parent = fs::path(root) / L"extensions", final = parent / (L"ublock-lite-" + version);
    if (fs::is_directory(final, ec)) {
        if (fs::is_regular_file(final / "manifest.json", ec) && ManifestVersion(final.wstring()) == version) { say(L"kept " + final.wstring()); return final.wstring(); }
        say(L"bundled folder (" + final.wstring() + L" is incomplete)"); return bundled;
    }
    const fs::path temp = parent / (L"ublock-lite-" + version + L".tmp-" + std::to_wstring(GetCurrentProcessId()));
    const int abandoned = RemoveAbandoned(parent);
    fs::remove_all(temp, ec);
    fs::create_directories(parent, ec);
    fs::copy(fs::path(bundled), temp, fs::copy_options::recursive, ec);
    if (ec) { std::error_code ignored; fs::remove_all(temp, ignored); say(L"bundled folder (copy failed)"); return bundled; }
    if (!MoveFileExW(temp.c_str(), final.c_str(), 0)) {   // another window may have published it first
        std::error_code ignored; fs::remove_all(temp, ignored);
        if (fs::is_regular_file(final / "manifest.json", ec) && ManifestVersion(final.wstring()) == version) { say(L"kept " + final.wstring() + L" (published by another window)"); return final.wstring(); }
        say(L"bundled folder (publish failed)"); return bundled;
    }
    say(L"copied to " + final.wstring() + (abandoned ? L" (" + std::to_wstring(abandoned) + L" unfinished copy removed)" : L""));
    return final.wstring();
}
// i18n-exempt-end

}  // namespace AdBlockFolder
