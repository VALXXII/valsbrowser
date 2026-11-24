#pragma once
// Chat intégré — where the code of a local site lives (design/chat-integration.md, « Site ↔ dossier »).
// Win32, no UI of its own except the Windows folder picker. Nothing here runs before the chat panel asks.
//
//   Detect(port)   the process LISTENING on the site's port (GetExtendedTcpTable, no admin rights) -> its working
//                  folder, read from that process (PEB -> ProcessParameters.CurrentDirectory; possible without
//                  elevation for a process of the same user at the same integrity level, refused otherwise: then
//                  there is simply no suggestion). The folder is walked up to the nearest project marker
//                  (package.json, .git…), never above the user's profile or a drive root.
//   Judge(...)     is that folder SURE? Sure = used at once, without the « Où est le code de ce site ? » screen:
//                  one process serves the port, its working folder IS the folder (not an ancestor found by walking
//                  up), the folder looks like a project (a marker file), no folder just above looks like the real
//                  project (a build output served from inside it), and it is not a wide folder (profile, Desktop,
//                  Documents, Downloads, a drive root, a system folder). The page's own file:line marks are checked
//                  by the caller (ChatPrompt::MarksInFolder). Anything else is uncertain: the folder is only
//                  PROPOSED and the user confirms it (« Utiliser ») or picks another one.
//   PickFolder     IFileOpenDialog in folder mode, opened on the most recent project folder.
// iphlpapi.dll and ntdll are reached through GetProcAddress: no new link dependency for the fixtures that
// include main.cpp.
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <algorithm>
#include <string>
#include <vector>

namespace ChatFolder {

struct Guess {
    std::wstring folder, process; DWORD pid = 0; std::wstring why;
    std::wstring cwd;      // the listener's working folder, as read
    std::vector<std::wstring> argFolders;   // folders named in its command line (« serve.py C:\…\landing-page 5173 », --root, --directory…)
    bool sure = false;     // Judge(): usable without asking (the page's marks still have to agree)
    std::wstring doubt;    // why it is not sure (log detail)
};

namespace detail {
// MIB_TCPTABLE_OWNER_PID / MIB_TCP6TABLE_OWNER_PID rows (declared here: no iphlpapi headers / import library).
struct Row4 { DWORD state, localAddr, localPort, remoteAddr, remotePort, pid; };
struct Row6 { UCHAR localAddr[16]; DWORD localScope, localPort; UCHAR remoteAddr[16]; DWORD remoteScope, remotePort, state, pid; };
using GetTable = DWORD(WINAPI*)(PVOID, PDWORD, BOOL, ULONG, int, ULONG);
// Every process listening on the port (IPv4 then IPv6), each once.
inline std::vector<DWORD> ListenerPids(int port) {
    std::vector<DWORD> found;
    HMODULE ip = LoadLibraryExW(L"iphlpapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); if (!ip) return found;
    auto get = reinterpret_cast<GetTable>(reinterpret_cast<void*>(GetProcAddress(ip, "GetExtendedTcpTable")));
    const auto add = [&](DWORD pid) { if (pid > 4 && std::find(found.begin(), found.end(), pid) == found.end()) found.push_back(pid); };
    const USHORT want = USHORT((port >> 8) | ((port & 0xFF) << 8));   // network byte order
    if (get) for (int family : {2 /* AF_INET */, 23 /* AF_INET6 */}) {
        DWORD size = 0; const int listeners = 3;   // TCP_TABLE_OWNER_PID_LISTENER
        if (get(nullptr, &size, FALSE, ULONG(family), listeners, 0) != ERROR_INSUFFICIENT_BUFFER || size == 0 || size > (8u << 20)) continue;
        std::vector<BYTE> buf(size + 4096); size = DWORD(buf.size());
        if (get(buf.data(), &size, FALSE, ULONG(family), listeners, 0) != NO_ERROR) continue;
        const DWORD n = *reinterpret_cast<const DWORD*>(buf.data());
        if (family == 2) { const auto* rows = reinterpret_cast<const Row4*>(buf.data() + sizeof(DWORD));
            for (DWORD i = 0; i < n && (BYTE*)(rows + i + 1) <= buf.data() + buf.size(); ++i) if (USHORT(rows[i].localPort) == want) add(rows[i].pid); }
        else { const auto* rows = reinterpret_cast<const Row6*>(buf.data() + sizeof(DWORD));
            for (DWORD i = 0; i < n && (BYTE*)(rows + i + 1) <= buf.data() + buf.size(); ++i) if (USHORT(rows[i].localPort) == want) add(rows[i].pid); }
    }
    FreeLibrary(ip);
    return found;
}
inline DWORD ListenerPid(int port) { const auto all = ListenerPids(port); return all.empty() ? 0 : all.front(); }
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
inline bool ProcessFolder(DWORD pid, std::wstring& cwd, std::wstring& image, std::wstring* commandLine = nullptr) {
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
            if (commandLine) *commandLine = ReadUnicode(p, params + 0x70, 8192);   // +0x70 CommandLine
            ok = !cwd.empty();
        }
    }
    (void)wow; CloseHandle(p);
    return ok;
#else
    (void)pid; (void)cwd; (void)image; (void)commandLine; return false;
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
// A file that says « a project lives here » by itself (a build tool's or a package manager's): unlike index.html or
// .git, which a build output or any folder of a repository may hold too.
inline bool HasProjectFile(const std::wstring& dir) {
    // i18n-exempt-begin: file names of project markers (never shown)
    for (const wchar_t* m : {L"package.json", L"astro.config.mjs", L"astro.config.ts", L"astro.config.js", L"vite.config.ts", L"vite.config.js", L"vite.config.mjs",
                             L"next.config.js", L"next.config.mjs", L"next.config.ts", L"pyproject.toml", L"composer.json", L"Gemfile"})
        if (Exists(dir + L"\\" + m)) return true;
    // i18n-exempt-end
    return false;
}
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

namespace detail {
// Wide folders a server may well run from, and that are never one project: the Desktop, Documents, Downloads, a
// OneDrive root (the profile, drive roots and system folders are refused outright by Refused).
inline bool TooWide(const std::wstring& dir) {
    const std::wstring d = Fold(Trim(dir)); wchar_t buf[MAX_PATH];
    if (const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH); n && n < MAX_PATH) {   // longer: buf not written (SEC-27)
        const std::wstring home = Fold(Trim(buf));
        for (const wchar_t* sub : {L"\\desktop", L"\\documents", L"\\downloads", L"\\bureau", L"\\téléchargements"}) if (d == home + sub) return true;   // i18n-exempt: folder names
    }
    for (const wchar_t* var : {L"OneDrive", L"OneDriveConsumer", L"OneDriveCommercial"}) {
        if (const DWORD n = GetEnvironmentVariableW(var, buf, MAX_PATH); !n || n >= MAX_PATH) continue;
        const std::wstring one = Fold(Trim(buf));
        for (const wchar_t* sub : {L"", L"\\desktop", L"\\documents", L"\\bureau"}) if (d == one + sub) return true;   // i18n-exempt: folder names
    }
    return false;
}
// A folder just above (3 levels at most, never a refused one) that holds a project file: the working folder is
// then probably a part of that project (dist\, public\, one package of several), and which one to give the agent is
// the user's call.
inline bool ProjectAbove(const std::wstring& dir) {
    std::wstring d = Parent(Trim(dir));
    for (int up = 0; up < 3 && !d.empty() && !Refused(d); ++up) {
        if (HasProjectFile(d)) return true;
        const std::wstring parent = Parent(d); if (parent == d) break; d = parent;
    }
    return false;
}
}
struct Certainty { bool sure = false; std::wstring doubt; };
// `cwd`: the listener's working folder; `folder`: the project root proposed for it; `listeners`: how many processes
// serve the port. Pure but for the file system.
// i18n-exempt-begin: `doubt` is a log detail (English diagnostics), never shown
inline Certainty Judge(const std::wstring& cwd, const std::wstring& folder, std::size_t listeners) {
    Certainty c;
    if (folder.empty()) c.doubt = L"no folder";
    else if (listeners != 1) c.doubt = std::to_wstring(listeners) + L" processes serve the port";
    else if (detail::Fold(detail::Trim(cwd)) != detail::Fold(detail::Trim(folder))) c.doubt = L"the project marker is above the server's working folder";
    else if (detail::Refused(folder) || detail::TooWide(folder)) c.doubt = L"the folder is too wide to be one project";
    else if (!detail::HasMarker(folder)) c.doubt = L"the folder does not look like a project (no package.json, index.html…)";
    else if (detail::ProjectAbove(folder)) c.doubt = L"a folder above looks like the project: this one may be a part of it";
    else c.sure = true;
    return c;
}
// i18n-exempt-end

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
// The project a folder belongs to, for a folder named by a server's arguments: the nearest ancestor (itself included,
// 6 levels at most) that holds a project file or a .git; the folder itself when none does. Unlike ProjectRoot, an
// index.html does not stop the walk: « serve.py web\landing » gives the repository, not web\landing.
inline std::wstring ProjectOf(const std::wstring& dir) {
    const std::wstring start = detail::Trim(dir);
    if (start.empty() || !detail::IsDir(start) || detail::Refused(start)) return L"";
    std::wstring d = start;
    for (int up = 0; up < 6 && !d.empty() && !detail::Refused(d) && !detail::TooWide(d); ++up) {
        if (detail::HasProjectFile(d) || detail::Exists(d + L"\\.git")) return d;   // i18n-exempt: file name
        const std::wstring parent = detail::Parent(d); if (parent == d) break; d = parent;
    }
    return start;
}
// The folders a server's command line names: each argument (or the value of « --root=… ») that is an existing folder,
// absolute or relative to the server's working folder, as the project it belongs to. Never the working folder itself
// (already a candidate), never a refused or too wide folder. 8 at most.
inline std::vector<std::wstring> ArgFolders(const std::wstring& commandLine, const std::wstring& cwd) {
    std::vector<std::wstring> out;
    if (commandLine.empty()) return out;
    int n = 0; LPWSTR* argv = CommandLineToArgvW(commandLine.c_str(), &n);
    if (!argv) return out;
    for (int i = 1; i < n && out.size() < 8; ++i) {
        std::wstring a = argv[i];
        if (a.size() > 2 && a[0] == L'-') { const auto eq = a.find(L'='); if (eq == std::wstring::npos) continue; a = a.substr(eq + 1); }
        if (a.empty() || a.size() > 1024) continue;
        for (auto& c : a) if (c == L'/') c = L'\\';
        std::wstring full = a;
        const bool absolute = a.size() > 2 && a[1] == L':' && a[2] == L'\\';
        if (!absolute) { if (cwd.empty() || a.find(L':') != std::wstring::npos) continue; full = detail::Trim(cwd) + L"\\" + a; }
        wchar_t buf[MAX_PATH * 2]; const DWORD got = GetFullPathNameW(full.c_str(), DWORD(sizeof buf / sizeof buf[0]), buf, nullptr);
        if (!got || got >= sizeof buf / sizeof buf[0] || !detail::IsDir(buf)) continue;
        const std::wstring project = ProjectOf(buf);
        if (project.empty() || detail::TooWide(project)) continue;
        if (std::find_if(out.begin(), out.end(), [&](const std::wstring& o) { return detail::Fold(o) == detail::Fold(project); }) == out.end()) out.push_back(project);
    }
    LocalFree(argv);
    return out;
}
// Every worktree of the git repository `dir` is in (the main one and those of « git worktree add »), `dir`'s own
// included; empty when it is not in a repository. Read from the files git keeps (.git, .git\worktrees\*\gitdir): no
// git process. 30 at most.
inline std::vector<std::wstring> RepoWorktrees(const std::wstring& dir) {
    std::vector<std::wstring> out;
    std::wstring d = detail::Trim(dir), dotGit;
    for (int up = 0; up < 8 && !d.empty() && !detail::Refused(d); ++up) {
        if (detail::Exists(d + L"\\.git")) { dotGit = d + L"\\.git"; break; }   // i18n-exempt: file name
        const std::wstring parent = detail::Parent(d); if (parent == d) break; d = parent;
    }
    if (dotGit.empty()) return out;
    const auto readLine = [](const std::wstring& file) {
        std::string text; HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return std::wstring();
        char buf[2048]; DWORD got = 0; if (ReadFile(h, buf, sizeof buf, &got, nullptr)) text.assign(buf, got); CloseHandle(h);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        if (const auto nl = text.find_first_of("\r\n"); nl != std::string::npos) text.erase(nl);
        std::wstring w; const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        if (n > 0) { w.resize(std::size_t(n)); MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), w.data(), n); }
        for (auto& c : w) if (c == L'/') c = L'\\';
        return w;
    };
    std::wstring common = dotGit;
    if (!detail::IsDir(dotGit)) {   // a worktree: « gitdir: <repo>\.git\worktrees\<name> »
        std::wstring line = readLine(dotGit);
        if (line.rfind(L"gitdir: ", 0) != 0) return out;   // i18n-exempt: git file syntax
        line = detail::Trim(line.substr(8));
        if (!(line.size() > 2 && line[1] == L':')) line = d + L"\\" + line;
        const std::wstring up1 = detail::Parent(line), up2 = detail::Parent(up1);   // …\.git\worktrees\<name> -> …\.git
        if (const auto cut = up1.find_last_of(L"\\"); cut == std::wstring::npos || detail::Fold(up1.substr(cut + 1)) != L"worktrees" || !detail::IsDir(up2)) return out;   // i18n-exempt: git folder name
        common = up2;
    }
    const auto add = [&](const std::wstring& w) {
        const std::wstring t = detail::Trim(w);
        if (t.empty() || !detail::IsDir(t) || out.size() >= 30) return;
        if (std::find_if(out.begin(), out.end(), [&](const std::wstring& o) { return detail::Fold(o) == detail::Fold(t); }) == out.end()) out.push_back(t);
    };
    add(detail::Parent(common));   // the main worktree
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((common + L"\\worktrees\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);   // i18n-exempt: git folder name
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            const std::wstring g = readLine(common + L"\\worktrees\\" + fd.cFileName + L"\\gitdir");   // i18n-exempt: git file name
            if (!g.empty()) add(detail::Parent(g));   // <worktree>\.git -> <worktree>
        } while (FindNextFileW(h, &fd) && out.size() < 30);
        FindClose(h);
    }
    return out;
}
// Off the UI thread. `port`: the site's port. Empty folder = no suggestion (why says it, for the log).
inline Guess Detect(int port) {
    Guess g;   // i18n-exempt-begin: `why` is a log detail (English diagnostics), never shown
    if (port < 1 || port > 65535) { g.why = L"no port"; return g; }
    const auto pids = detail::ListenerPids(port);
    g.pid = pids.empty() ? 0 : pids.front();
    if (!g.pid) { g.why = L"no listener found on port " + std::to_wstring(port); return g; }
    if (g.pid == GetCurrentProcessId()) { g.why = L"the listener is the browser itself"; return g; }
    std::wstring cwd, commandLine;
    if (!detail::ProcessFolder(g.pid, cwd, g.process, &commandLine)) { g.why = L"the listener's working folder cannot be read (another user or elevated process)"; return g; }
    g.cwd = cwd; g.folder = ProjectRoot(cwd); g.argFolders = ArgFolders(commandLine, cwd);
    if (g.folder.empty()) g.why = L"the listener's working folder is not a project folder: " + cwd;
    const Certainty c = Judge(cwd, g.folder, pids.size()); g.sure = c.sure; g.doubt = c.doubt;
    return g;   // i18n-exempt-end
}
// Is the page served from this folder? `served`: the URL paths of the files the page loads (« /app.js »,
// « /css/site.css », the document when it names an .html file), as the page script lists them. Off the UI thread
// (it reads the folder). A web server serves one folder of the project (the project itself, public\, web\landing\…):
// the paths must all be found under ONE such base folder (itself or a sub-folder, 3 levels at most), each directly or
// in its public\ / static\ sub-folder (where Vite, Astro… serve them from). One may be missing when 4 or more are named.
//   None     nothing to check (no path that can be a file on disk: routes, /_next/…, /@vite/…): impossible to say
//   Inside   the page's files are in the folder
//   Outside  they are not: the page comes from another folder
enum class Served { None, Inside, Outside };
inline std::vector<std::wstring> ServedFiles(const std::vector<std::string>& served) {
    std::vector<std::wstring> out;
    for (const auto& raw : served) {
        if (raw.empty() || raw.size() > 1024) continue;
        std::wstring p; p.reserve(raw.size());
        { const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), nullptr, 0); if (n <= 0) continue; p.resize(std::size_t(n)); MultiByteToWideChar(CP_UTF8, 0, raw.data(), int(raw.size()), p.data(), n); }
        if (const auto q = p.find_first_of(L"?#"); q != std::wstring::npos) p.erase(q);
        for (auto& c : p) if (c == L'\\') c = L'/';
        while (!p.empty() && p.front() == L'/') p.erase(0, 1);
        if (p.empty() || p.back() == L'/') p += L"index.html";
        const std::wstring low = detail::Fold(p);
        // i18n-exempt-begin: URL prefixes of dev servers' virtual files (never shown)
        bool skip = low.find(L':') != std::wstring::npos;
        for (const wchar_t* v : {L"@", L"_next\\", L"_astro\\", L"_nuxt\\", L"_app\\", L"__", L"node_modules\\", L".well-known\\", L"cdn-cgi\\", L"_vercel\\"})
            if (low.rfind(v, 0) == 0) skip = true;
        // i18n-exempt-end
        std::wstring seg; bool bad = false;
        for (std::size_t i = 0; i <= low.size(); ++i) { if (i == low.size() || low[i] == L'\\') { if (seg == L".." || seg == L".") bad = true; seg.clear(); } else seg += low[i]; }
        const auto slash = low.find_last_of(L'\\'); const std::wstring name = slash == std::wstring::npos ? low : low.substr(slash + 1);
        if (skip || bad || name.find(L'.') == std::wstring::npos) continue;   // a route (« /about »), not a file
        for (auto& c : p) if (c == L'/') c = L'\\';
        if (std::find_if(out.begin(), out.end(), [&](const std::wstring& o) { return detail::Fold(o) == low; }) == out.end() && out.size() < 24) out.push_back(p);
    }
    return out;
}
inline Served ServedInFolder(const std::vector<std::string>& served, const std::wstring& folder) {
    const std::vector<std::wstring> files = ServedFiles(served);
    if (files.empty()) return Served::None;
    const std::wstring root = detail::Trim(folder);
    if (root.empty() || !detail::IsDir(root)) return Served::Outside;
    const auto isFile = [](const std::wstring& p) { const DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); };
    const std::size_t need = files.size() >= 4 ? files.size() - 1 : files.size();
    const auto hits = [&](const std::wstring& base) {
        std::size_t n = 0;
        for (const auto& f : files) if (isFile(base + L"\\" + f) || isFile(base + L"\\public\\" + f) || isFile(base + L"\\static\\" + f)) ++n;   // i18n-exempt: folder names
        return n;
    };
    // the folder, then its sub-folders breadth first (3 levels, 400 folders at most; never a link, a hidden folder,
    // node_modules, nor a nested worktree)
    std::vector<std::pair<std::wstring, int>> todo{{root, 0}};
    for (std::size_t i = 0; i < todo.size() && i < 400; ++i) {
        if (hits(todo[i].first) >= need) return Served::Inside;
        if (todo[i].second >= 3) continue;
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((todo[i].first + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || fd.cFileName[0] == L'.') continue;
            const std::wstring n = detail::Fold(fd.cFileName);
            if (n == L"node_modules" || n == L"worktrees") continue;   // i18n-exempt: folder names
            if (todo.size() < 400) todo.push_back({todo[i].first + L"\\" + fd.cFileName, todo[i].second + 1});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return Served::Outside;
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
