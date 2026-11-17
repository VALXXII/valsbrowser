#pragma once
// Chat intégré — where the code of a local site lives (design/chat-integration.md, « Site ↔ dossier »).
// Win32, no UI of its own except the Windows folder picker. Nothing here runs before the chat panel asks.
//
//   Detect(port)   the process LISTENING on the site's port (GetExtendedTcpTable, no admin rights) -> its working
//                  folder, read from that process (PEB -> ProcessParameters.CurrentDirectory; possible without
//                  elevation for a process of the same user at the same integrity level, refused otherwise: then
//                  there is simply no suggestion). The folder is walked up to the nearest project marker
//                  (package.json, .git…), never above the user's profile or a drive root. A suggestion is only
//                  ever PROPOSED: the user confirms it (« Utiliser ») or picks another folder.
//   PickFolder     IFileOpenDialog in folder mode, opened on the most recent project folder.
// iphlpapi.dll and ntdll are reached through GetProcAddress: no new link dependency for the fixtures that
// include main.cpp.
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <string>
#include <vector>

namespace ChatFolder {

struct Guess { std::wstring folder, process; DWORD pid = 0; std::wstring why; };

namespace detail {
// MIB_TCPTABLE_OWNER_PID / MIB_TCP6TABLE_OWNER_PID rows (declared here: no iphlpapi headers / import library).
struct Row4 { DWORD state, localAddr, localPort, remoteAddr, remotePort, pid; };
struct Row6 { UCHAR localAddr[16]; DWORD localScope, localPort; UCHAR remoteAddr[16]; DWORD remoteScope, remotePort, state, pid; };
using GetTable = DWORD(WINAPI*)(PVOID, PDWORD, BOOL, ULONG, int, ULONG);
inline DWORD ListenerPid(int port) {
    HMODULE ip = LoadLibraryExW(L"iphlpapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); if (!ip) return 0;
    auto get = reinterpret_cast<GetTable>(reinterpret_cast<void*>(GetProcAddress(ip, "GetExtendedTcpTable")));
    DWORD found = 0;
    const USHORT want = USHORT((port >> 8) | ((port & 0xFF) << 8));   // network byte order
    if (get) for (int family : {2 /* AF_INET */, 23 /* AF_INET6 */}) {
        if (found) break;
        DWORD size = 0; const int listeners = 3;   // TCP_TABLE_OWNER_PID_LISTENER
        if (get(nullptr, &size, FALSE, ULONG(family), listeners, 0) != ERROR_INSUFFICIENT_BUFFER || size == 0 || size > (8u << 20)) continue;
        std::vector<BYTE> buf(size + 4096); size = DWORD(buf.size());
        if (get(buf.data(), &size, FALSE, ULONG(family), listeners, 0) != NO_ERROR) continue;
        const DWORD n = *reinterpret_cast<const DWORD*>(buf.data());
        if (family == 2) { const auto* rows = reinterpret_cast<const Row4*>(buf.data() + sizeof(DWORD));
            for (DWORD i = 0; i < n && (BYTE*)(rows + i + 1) <= buf.data() + buf.size(); ++i) if (USHORT(rows[i].localPort) == want && rows[i].pid > 4) { found = rows[i].pid; break; } }
        else { const auto* rows = reinterpret_cast<const Row6*>(buf.data() + sizeof(DWORD));
            for (DWORD i = 0; i < n && (BYTE*)(rows + i + 1) <= buf.data() + buf.size(); ++i) if (USHORT(rows[i].localPort) == want && rows[i].pid > 4) { found = rows[i].pid; break; } }
    }
    FreeLibrary(ip);
    return found;
}
struct BasicInfo { PVOID reserved1; PVOID peb; PVOID reserved2[2]; ULONG_PTR pid; PVOID reserved3; };
using NtQuery = LONG(WINAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
inline bool ReadAt(HANDLE p, ULONG_PTR at, void* out, SIZE_T n) { SIZE_T got = 0; return ReadProcessMemory(p, reinterpret_cast<LPCVOID>(at), out, n, &got) && got == n; }
// A UNICODE_STRING { USHORT Length, MaximumLength; PWSTR Buffer } of another x64 process.
inline std::wstring ReadUnicode(HANDLE p, ULONG_PTR at, size_t maxChars) {
    struct { USHORT length, maximum; ULONG pad; ULONG_PTR buffer; } u{};
    if (!ReadAt(p, at, &u, sizeof u) || !u.buffer || u.length == 0 || u.length / 2 > maxChars) return L"";
    std::wstring s(u.length / 2, L'\0');
    return ReadAt(p, u.buffer, s.data(), u.length) ? s : std::wstring();
}
// Working folder and image name of a process of the same user (x64 layout: PEB+0x20 -> ProcessParameters;
// +0x38 CurrentDirectory.DosPath, +0x60 ImagePathName). Empty when Windows refuses (another user, elevated).
inline bool ProcessFolder(DWORD pid, std::wstring& cwd, std::wstring& image) {
#if defined(_WIN64)
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid); if (!p) return false;
    bool ok = false;
    BOOL wow = FALSE; IsWow64Process(p, &wow);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto query = nt ? reinterpret_cast<NtQuery>(reinterpret_cast<void*>(GetProcAddress(nt, "NtQueryInformationProcess"))) : nullptr;
    BasicInfo bi{}; ULONG len = 0;
    if (query && query(p, 0 /* ProcessBasicInformation */, &bi, sizeof bi, &len) >= 0 && bi.peb) {
        ULONG_PTR params = 0;
        if (ReadAt(p, reinterpret_cast<ULONG_PTR>(bi.peb) + 0x20, &params, sizeof params) && params) {
            cwd = ReadUnicode(p, params + 0x38, 1024); image = ReadUnicode(p, params + 0x60, 1024);
            ok = !cwd.empty();
        }
    }
    (void)wow; CloseHandle(p);
    return ok;
#else
    (void)pid; (void)cwd; (void)image; return false;
#endif
}
inline bool IsDir(const std::wstring& p) { const DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
inline bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
inline std::wstring Trim(std::wstring p) { while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back(); return p; }
inline std::wstring Parent(const std::wstring& p) { const auto s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? L"" : (s <= 2 ? p.substr(0, 3) : p.substr(0, s)); }
inline bool HasMarker(const std::wstring& dir) {
    // i18n-exempt-begin: file names of project markers (never shown)
    for (const wchar_t* m : {L"package.json", L".git", L"astro.config.mjs", L"astro.config.ts", L"vite.config.ts", L"vite.config.js", L"next.config.js", L"next.config.mjs",
                             L"pyproject.toml", L"composer.json", L"Gemfile", L"index.html"})
        if (Exists(dir + L"\\" + m)) return true;
    // i18n-exempt-end
    return false;
}
inline std::wstring Fold(std::wstring s) { for (auto& c : s) { if (c == L'/') c = L'\\'; c = towlower(c); } return s; }
// Folders that are never a project: a drive root, the Windows folder, the user's profile itself, system temp roots.
inline bool Refused(const std::wstring& dir) {
    const std::wstring d = Fold(Trim(dir));
    if (d.size() <= 3) return true;
    wchar_t buf[MAX_PATH];
    if (GetWindowsDirectoryW(buf, MAX_PATH) && (d == Fold(buf) || d.rfind(Fold(buf) + L"\\", 0) == 0)) return true;
    if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH) && d == Fold(Trim(buf))) return true;
    if (GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH) && d.rfind(Fold(buf), 0) == 0) return true;
    return false;
}
}

// The project root for a working folder: the nearest ancestor (itself included, 6 levels at most) holding a
// project marker; the folder itself when none does. Empty when the folder is refused (drive root, profile…).
inline std::wstring ProjectRoot(const std::wstring& cwd) {
    const std::wstring start = detail::Trim(cwd);
    if (start.empty() || !detail::IsDir(start) || detail::Refused(start)) return L"";
    std::wstring d = start;
    for (int up = 0; up < 6 && !d.empty() && !detail::Refused(d); ++up) {
        if (detail::HasMarker(d)) return d;
        const std::wstring parent = detail::Parent(d); if (parent == d) break; d = parent;
    }
    return start;
}
// Off the UI thread. `port`: the site's port. Empty folder = no suggestion (why says it, for the log).
inline Guess Detect(int port) {
    Guess g;   // i18n-exempt-begin: `why` is a log detail (English diagnostics), never shown
    if (port < 1 || port > 65535) { g.why = L"no port"; return g; }
    g.pid = detail::ListenerPid(port);
    if (!g.pid) { g.why = L"no listener found on port " + std::to_wstring(port); return g; }
    if (g.pid == GetCurrentProcessId()) { g.why = L"the listener is the browser itself"; return g; }
    std::wstring cwd;
    if (!detail::ProcessFolder(g.pid, cwd, g.process)) { g.why = L"the listener's working folder cannot be read (another user or elevated process)"; return g; }
    g.folder = ProjectRoot(cwd);
    if (g.folder.empty()) g.why = L"the listener's working folder is not a project folder: " + cwd;
    return g;   // i18n-exempt-end
}
// UI thread (modal). Empty = cancelled. `initial`: a folder to open the dialog on (may be empty).
inline std::wstring PickFolder(HWND owner, const std::wstring& title, const std::wstring& initial) {
    std::wstring out; IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg) return out;
    DWORD opts = 0; dlg->GetOptions(&opts); dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    if (!title.empty()) dlg->SetTitle(title.c_str());
    if (!initial.empty()) { IShellItem* at = nullptr; if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&at))) && at) { dlg->SetFolder(at); at->Release(); } }
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) { PWSTR path = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) { out = path; CoTaskMemFree(path); } item->Release(); }
    }
    dlg->Release();
    return detail::Trim(out);
}
// « C:\Users\…\projets\boutique »: the middle of a long path folded, the last two folders kept.
inline std::wstring Display(const std::wstring& path) {
    if (path.size() <= 38) return path;
    std::vector<std::wstring> parts; std::wstring cur;
    for (wchar_t c : path) { if (c == L'\\' || c == L'/') { if (!cur.empty()) parts.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.size() < 5) return path;
    return parts[0] + L"\\" + parts[1] + L"\\…\\" + parts[parts.size() - 2] + L"\\" + parts.back();
}
inline std::wstring Name(const std::wstring& path) { const std::wstring p = detail::Trim(path); const auto s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? p : p.substr(s + 1); }

} // namespace ChatFolder
