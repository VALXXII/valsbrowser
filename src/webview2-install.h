#pragma once
#include "i18n.h"
// Evergreen WebView2 Runtime install through Microsoft's bootstrapper, started only by the user
// from the startup recovery dialog (src/webview2-runtime.h, design/webview2-startup-recovery.md).
//
//   1. GET https://go.microsoft.com/fwlink/p/?LinkId=2124703 — the « Get the Link » address of
//      https://developer.microsoft.com/microsoft-edge/webview2/ (Evergreen Bootstrapper). HTTPS
//      only (TLS 1.2/1.3, system certificate validation untouched); WinHTTP's own redirects,
//      cookies and authentication are switched off and checked (fail closed); redirects are
//      followed manually and only to https://microsoft.com or https://*.microsoft.com on port 443;
//      ≤ 16 MiB and ≤ 5 min in total.
//   2. Written into a fresh directory with a random name under the user's temp folder, DACL
//      protected (current user + SYSTEM); re-opened read-only, deny-write/deny-delete, and that
//      handle stays open through verification and execution.
//   3. WinVerifyTrust (WINTRUST_ACTION_GENERIC_VERIFY_V2, whole-chain revocation) on that handle,
//      then the signer chain must end in a Microsoft root (CertVerifyCertificateChainPolicy,
//      CERT_CHAIN_POLICY_MICROSOFT_ROOT) AND the signing certificate must be CN/O « Microsoft
//      Corporation », AND the version resource read from that same locked image (no MUI file)
//      must name MicrosoftEdgeUpdateSetup.exe. That proves « Microsoft Edge Update setup signed by
//      Microsoft, served by the official link »; which product it installs is carried by a tag
//      outside the Authenticode hash, so « WebView2 » itself is not proven by the signature.
//   4. One installer per user at a time, across processes and logon sessions: a per-user record
//      (host identity, then setup pid/creation/thread, then `resumed`) guarded by an exclusive
//      per-user lock file, reserved before the download; the setup is created SUSPENDED, recorded,
//      the cancel request is checked, and only then is it resumed. If the launching browser dies
//      anywhere in between, the next check resumes the recorded setup (a no-op if it already runs),
//      so it is never left blocking, never killed and never doubled (see Settle) — unless the user
//      withdrew the request: a « Quitter » is decided atomically with the launch (LaunchGate: the
//      never-resumed setup is terminated by its host), recorded as `cancelled=1`, and a withdrawn
//      request is never resumed.
//      Command: "<file>" /silent /install — documented; no shell, no inherited handles, no
//      elevation (an unelevated run installs per-user, distribution doc).
//   5. The file and directory are removed afterwards; while the installer runs they stay, and the
//      next claim after its exit removes exactly the folder recorded in the marker.
//
// Link-free: winhttp/wintrust/crypt32 are resolved at run time from System32 only, so this header
// compiles into every build of main.cpp (stamped, preview, unstamped fixtures) without new link
// flags, and the updater's GitHub transport (update-core) is not touched.
// Tests replace the network, trust and process steps through `Effects` and the API table; nothing
// in production reads a variable or flag that could relax the URL, TLS or signature checks.
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "webview2-runtime.h"

namespace WebView2Install {

inline const wchar_t* BootstrapperUrl() { return L"https://go.microsoft.com/fwlink/p/?LinkId=2124703"; }
// For the user (distribution doc, « Download Microsoft Edge WebView2 »).
inline const wchar_t* ManualPageUrl() { return L"https://developer.microsoft.com/microsoft-edge/webview2/consumer/"; }
inline const wchar_t* ExpectedOriginalName() { return L"MicrosoftEdgeUpdateSetup.exe"; }
inline const wchar_t* FileName() { return L"MicrosoftEdgeWebview2Setup.exe"; }
inline constexpr std::uint64_t MaxBytes = 16ull << 20;
inline constexpr DWORD InstallTimeoutMs = 10 * 60 * 1000;
inline constexpr DWORD DownloadDeadlineMs = 5 * 60 * 1000;

// ---------------------------------------------------------------- URL policy
inline std::wstring Lower(std::wstring s) { for (auto& c : s) if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a'); return s; }
struct HttpsUrl { std::wstring host, path; };
// Strict parser: "https://" host [":443"] [path/query]; no userinfo, no other port, ASCII only.
inline bool ParseHttpsUrl(const std::wstring& url, HttpsUrl& out) {
    if (url.size() > 4096) return false;
    for (wchar_t c : url) if (c <= 0x20 || c >= 0x7f || c == L'\\') return false;
    const std::wstring scheme = L"https://";
    if (url.size() <= scheme.size() || Lower(url.substr(0, scheme.size())) != scheme) return false;
    const size_t start = scheme.size();
    size_t end = url.find_first_of(L"/?#", start);
    std::wstring authority = url.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    if (authority.empty() || authority.find(L'@') != std::wstring::npos) return false;
    const size_t colon = authority.find(L':');
    if (colon != std::wstring::npos) { if (authority.substr(colon) != L":443") return false; authority.resize(colon); }
    if (authority.empty() || authority.front() == L'.' || authority.back() == L'.' || authority.find(L"..") != std::wstring::npos) return false;
    for (wchar_t c : authority)
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'.')) return false;
    out.host = Lower(authority);
    std::wstring rest = end == std::wstring::npos ? L"/" : url.substr(end);
    const size_t hash = rest.find(L'#');
    if (hash != std::wstring::npos) rest.resize(hash);
    if (rest.empty() || rest.front() == L'?') rest = L"/" + rest;
    out.path = rest;
    return true;
}
// Real DNS suffix: "microsoft.com" itself or a name ending in ".microsoft.com" (never
// "evilmicrosoft.com" nor "microsoft.com.evil.net").
inline bool IsMicrosoftHost(const std::wstring& hostLower) {
    static const std::wstring apex = L"microsoft.com", suffix = L".microsoft.com";
    return hostLower == apex || (hostLower.size() > suffix.size() && hostLower.compare(hostLower.size() - suffix.size(), suffix.size(), suffix) == 0);
}
inline bool AllowedUrl(const std::wstring& url) { HttpsUrl u; return ParseHttpsUrl(url, u) && IsMicrosoftHost(u.host); }

// ---------------------------------------------------------------- system APIs (System32 only)
template <class T> inline bool Resolve(HMODULE m, const char* name, T& out) {
    out = m ? reinterpret_cast<T>(reinterpret_cast<void (*)()>(GetProcAddress(m, name))) : nullptr;
    return out != nullptr;
}
// Function table. Get() is the System32 one; tests copy it and replace entries (no network).
class Api {
public:
    static const Api& Get() { static const Api api = Load(); return api; }
    bool http = false, trust = false;
    decltype(&::WinHttpOpen) Open = nullptr; decltype(&::WinHttpSetTimeouts) SetTimeouts = nullptr;
    decltype(&::WinHttpSetOption) SetOption = nullptr; decltype(&::WinHttpConnect) Connect = nullptr;
    decltype(&::WinHttpOpenRequest) OpenRequest = nullptr; decltype(&::WinHttpSendRequest) SendRequest = nullptr;
    decltype(&::WinHttpReceiveResponse) ReceiveResponse = nullptr; decltype(&::WinHttpQueryHeaders) QueryHeaders = nullptr;
    decltype(&::WinHttpReadData) ReadData = nullptr; decltype(&::WinHttpCloseHandle) CloseHandle = nullptr;
    decltype(&::WinVerifyTrust) VerifyTrust = nullptr; decltype(&::WTHelperProvDataFromStateData) ProvData = nullptr;
    decltype(&::WTHelperGetProvSignerFromChain) ProvSigner = nullptr;
    decltype(&::CertVerifyCertificateChainPolicy) ChainPolicy = nullptr; decltype(&::CertGetNameStringW) NameString = nullptr;
    static Api Load() {
        Api a;
        auto load = [](const wchar_t* dll) { return LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); };
        HMODULE h = load(L"winhttp.dll"), t = load(L"wintrust.dll"), c = load(L"crypt32.dll");
        a.http = Resolve(h, "WinHttpOpen", a.Open) & Resolve(h, "WinHttpSetTimeouts", a.SetTimeouts) & Resolve(h, "WinHttpSetOption", a.SetOption) &
                 Resolve(h, "WinHttpConnect", a.Connect) & Resolve(h, "WinHttpOpenRequest", a.OpenRequest) & Resolve(h, "WinHttpSendRequest", a.SendRequest) &
                 Resolve(h, "WinHttpReceiveResponse", a.ReceiveResponse) & Resolve(h, "WinHttpQueryHeaders", a.QueryHeaders) &
                 Resolve(h, "WinHttpReadData", a.ReadData) & Resolve(h, "WinHttpCloseHandle", a.CloseHandle);
        a.trust = Resolve(t, "WinVerifyTrust", a.VerifyTrust) & Resolve(t, "WTHelperProvDataFromStateData", a.ProvData) &
                  Resolve(t, "WTHelperGetProvSignerFromChain", a.ProvSigner) & Resolve(c, "CertVerifyCertificateChainPolicy", a.ChainPolicy) &
                  Resolve(c, "CertGetNameStringW", a.NameString);
        return a;
    }
};

// ---------------------------------------------------------------- fetch
struct FetchResult {
    enum Status { Ok, Network, Http, Rejected, TooLarge, Cancelled, TimedOut } status = Network;
    DWORD code = 0;   // HTTP status or Win32 error
};
class Fetch {
public:
    virtual ~Fetch() = default;
    // `sink` receives the body; `progress(received, total)` (total 0 = unknown).
    virtual FetchResult Get(const std::wstring& url, std::uint64_t maxBytes, const std::function<bool(const void*, std::size_t)>& sink,
                            const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>& cancel) = 0;
    virtual void Abort() {}   // thread-safe: unblocks an in-flight Get
};

// WinHTTP with the same limits as the updater's transport (update-core.cpp): system proxy,
// 10/10/15/20 s per-operation timeouts, TLS 1.2 (+1.3), ≤ 6 manual redirects — plus a total
// deadline. Automatic redirects, cookies and authentication are disabled at session level
// (redirect policy NEVER) and per request (DISABLE_FEATURE); if either cannot be set, the
// request is refused before it is sent (fail closed).
class WinHttpFetch : public Fetch {
public:
    explicit WinHttpFetch(const Api& api = Api::Get(), DWORD deadlineMs = DownloadDeadlineMs) : api_(api), deadlineMs_(deadlineMs) {
        if (!api_.http) { error_ = ERROR_PROC_NOT_FOUND; return; }
        session_ = api_.Open(L"browser-webview2-setup/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) session_ = api_.Open(L"browser-webview2-setup/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) { error_ = GetLastError(); return; }
        api_.SetTimeouts(session_, 10000, 10000, 15000, 20000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!api_.SetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            api_.SetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
        DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!api_.SetOption(session_, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never))) {
            error_ = GetLastError(); api_.CloseHandle(session_); session_ = nullptr;   // fail closed
        }
    }
    ~WinHttpFetch() override { if (session_) api_.CloseHandle(session_); }
    void Abort() override {
        std::lock_guard<std::mutex> lock(mutex_);
        aborted_ = true;
        if (request_) { api_.CloseHandle(request_); request_ = nullptr; }
    }
    FetchResult Get(const std::wstring& startUrl, std::uint64_t maxBytes, const std::function<bool(const void*, std::size_t)>& sink,
                    const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>& cancel) override {
        FetchResult r;
        if (!session_) { r.status = FetchResult::Rejected; r.code = error_ ? error_ : ERROR_INVALID_HANDLE; return r; }
        const ULONGLONG deadline = GetTickCount64() + deadlineMs_;
        auto late = [&] { return GetTickCount64() > deadline; };
        std::wstring url = startUrl;
        for (int hop = 0; hop < 6; ++hop) {
            if (cancel.load()) return r.status = FetchResult::Cancelled, r;
            if (late()) return r.status = FetchResult::TimedOut, r.code = ERROR_TIMEOUT, r;
            HttpsUrl u;
            if (!ParseHttpsUrl(url, u) || !IsMicrosoftHost(u.host)) return r.status = FetchResult::Rejected, r;
            HINTERNET connect = api_.Connect(session_, u.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!connect) return r.code = GetLastError(), r;
            HINTERNET request = api_.OpenRequest(connect, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (!request) { r.code = GetLastError(); api_.CloseHandle(connect); return r; }
            DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
            if (!api_.SetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable))) {
                r.status = FetchResult::Rejected; r.code = GetLastError();   // never send a request that could follow redirects itself
                api_.CloseHandle(request); api_.CloseHandle(connect);
                return r;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (aborted_ || cancel.load()) { api_.CloseHandle(request); api_.CloseHandle(connect); return r.status = FetchResult::Cancelled, r; }
                request_ = request;
            }
            auto finish = [&](FetchResult res) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (request_) api_.CloseHandle(request_);
                    request_ = nullptr;
                    if (aborted_ || cancel.load()) res.status = FetchResult::Cancelled;
                }
                api_.CloseHandle(connect);
                return res;
            };
            if (!api_.SendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !api_.ReceiveResponse(request, nullptr)) {
                r.code = GetLastError();   // includes ERROR_WINHTTP_SECURE_FAILURE: TLS is never relaxed
                return finish(r);
            }
            DWORD status = 0, size = sizeof(status);
            api_.QueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                wchar_t location[4096]; DWORD bytes = sizeof(location);
                if (!api_.QueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location, &bytes, WINHTTP_NO_HEADER_INDEX)) {
                    r.status = FetchResult::Http; r.code = status; return finish(r);
                }
                std::wstring next(location, bytes / sizeof(wchar_t));
                if (!AllowedUrl(next)) { r.status = FetchResult::Rejected; return finish(r); }
                finish(r);
                url = next;
                continue;
            }
            if (status != 200) { r.status = FetchResult::Http; r.code = status; return finish(r); }
            std::uint64_t length = 0; wchar_t text[32]; DWORD textBytes = sizeof(text);
            if (api_.QueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &textBytes, WINHTTP_NO_HEADER_INDEX))
                length = std::wcstoull(text, nullptr, 10);
            if (length > maxBytes) { r.status = FetchResult::TooLarge; return finish(r); }
            std::vector<char> buffer(64 * 1024);
            std::uint64_t total = 0;
            for (;;) {
                if (cancel.load()) { r.status = FetchResult::Cancelled; return finish(r); }
                if (late()) { r.status = FetchResult::TimedOut; r.code = ERROR_TIMEOUT; return finish(r); }   // a trickling server
                DWORD got = 0;
                if (!api_.ReadData(request, buffer.data(), DWORD(buffer.size()), &got)) { r.code = GetLastError(); return finish(r); }
                if (!got) break;
                total += got;
                if (total > maxBytes) { r.status = FetchResult::TooLarge; return finish(r); }
                if (!sink(buffer.data(), got)) { r.status = FetchResult::Network; r.code = ERROR_WRITE_FAULT; return finish(r); }
                if (progress) progress(total, length);
            }
            if (length && total != length) { r.status = FetchResult::Network; r.code = ERROR_HANDLE_EOF; return finish(r); }
            r.status = FetchResult::Ok; r.code = 200;
            return finish(r);
        }
        r.status = FetchResult::Rejected;   // too many redirects
        return r;
    }
private:
    Api api_;
    DWORD deadlineMs_;
    DWORD error_ = 0;
    HINTERNET session_ = nullptr;
    std::mutex mutex_;
    HINTERNET request_ = nullptr;
    bool aborted_ = false;
};

// ---------------------------------------------------------------- identity helpers
// RtlGenRandom (advapi32 SystemFunction036), the CSPRNG behind rand_s.
inline bool Random(void* out, ULONG size) {
    using Gen = BOOLEAN(WINAPI*)(PVOID, ULONG);
    static Gen gen = [] { Gen g = nullptr; Resolve(LoadLibraryExW(L"advapi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "SystemFunction036", g); return g; }();
    return gen && gen(out, size);
}
inline DWORD UserSid(std::wstring& out) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return GetLastError();
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size ? size : 1);
    DWORD error = ERROR_SUCCESS;
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) error = GetLastError();
    else {
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text)) error = GetLastError();
        else { out = text; LocalFree(text); }
    }
    CloseHandle(token);
    return error;
}
// A folder this installer could have created: <anything>\browser-wv2-<24 lowercase hex>.
inline bool IsPrivateDirPath(const std::wstring& path) {
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash == 0) return false;
    const std::wstring name = path.substr(slash + 1), prefix = L"browser-wv2-";
    if (name.size() != prefix.size() + 24 || name.compare(0, prefix.size(), prefix) != 0) return false;
    for (size_t i = prefix.size(); i < name.size(); ++i) if (!((name[i] >= L'0' && name[i] <= L'9') || (name[i] >= L'a' && name[i] <= L'f'))) return false;
    return true;
}
// Removes only the setup this installer wrote (by name), then the folder if it is then empty.
inline bool RemovePrivateDir(const std::wstring& path) {
    if (!IsPrivateDirPath(path)) return false;
    DeleteFileW((path + L"\\" + FileName()).c_str());
    return RemoveDirectoryW(path.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

// ---------------------------------------------------------------- private directory
// Random name (RtlGenRandom), protected DACL: the current user and SYSTEM only.
class PrivateDir {
public:
    PrivateDir() = default;
    PrivateDir(const PrivateDir&) = delete;
    PrivateDir& operator=(const PrivateDir&) = delete;
    ~PrivateDir() { Remove(); }
    DWORD Create(std::wstring parent) {
        if (parent.empty()) {
            wchar_t temp[MAX_PATH + 1]; DWORD n = GetTempPathW(MAX_PATH + 1, temp);
            if (!n || n > MAX_PATH) return ERROR_PATH_NOT_FOUND;
            parent.assign(temp, n);
        }
        if (!parent.empty() && parent.back() != L'\\') parent += L'\\';
        std::wstring sid;
        if (DWORD e = UserSid(sid)) return e;
        const std::wstring sddl = L"D:P(A;OICI;FA;;;" + sid + L")(A;OICI;FA;;;SY)";
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return GetLastError();
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        DWORD result = ERROR_ALREADY_EXISTS;
        for (int tries = 0; tries < 4 && result == ERROR_ALREADY_EXISTS; ++tries) {
            unsigned int a = 0, b = 0, c = 0;
            if (!Random(&a, sizeof(a)) || !Random(&b, sizeof(b)) || !Random(&c, sizeof(c))) { result = ERROR_GEN_FAILURE; break; }
            wchar_t name[40]; swprintf(name, 40, L"browser-wv2-%08x%08x%08x", a, b, c);
            const std::wstring path = parent + name;
            if (CreateDirectoryW(path.c_str(), &sa)) { path_ = path; result = ERROR_SUCCESS; }
            else result = GetLastError();
        }
        LocalFree(sd);
        return result;
    }
    const std::wstring& Path() const { return path_; }
    std::wstring Name() const { const size_t s = path_.find_last_of(L'\\'); return s == std::wstring::npos ? path_ : path_.substr(s + 1); }
    bool Remove() {
        if (path_.empty()) return true;
        const bool ok = RemovePrivateDir(path_);
        if (ok) path_.clear();
        return ok;
    }
    void Release() { path_.clear(); }   // left to the running installer (recorded in the claim marker)
private:
    std::wstring path_;
};

// ---------------------------------------------------------------- one installer per user
// Per-user record %LOCALAPPDATA%\browser\webview2-install.lock, read and written only while holding
// the per-user lock file next to it (exclusive open, share 0: the OS releases it when its holder
// dies; %LOCALAPPDATA% is the same for every logon session of the user, so the lock is per user,
// not per session — a Local\ mutex was per session).
//
// Record: host (pid + creation time of the browser process that launches), pid/created/tid of the
// setup once created, `resumed` once its main thread was resumed, and its private folder.
// Life cycle, all under the lock:
//   1. claim:   written BEFORE CreateProcess with the host identity only (pid = 0);
//   2. created: the setup exists SUSPENDED; pid + creation time + main thread id recorded;
//   3. resumed: after ResumeThread, `resumed=1`;
//   4. release: when the setup has exited (or was never resumed and was terminated by its host).
// Evaluation by any later check (Settle):
//   - setup gone (pid dead or creation time differs: PID reuse) and host gone → stale, folder removed;
//   - host alive → busy (the host manages its own launch, including cancellation);
//   - host gone, setup alive → busy, and if `resumed` is not recorded the setup's main thread is
//     resumed (ResumeThread). That is a no-op on a thread already running (suspend count 0) and
//     otherwise starts the install the user asked for. The host may have died anywhere around its
//     own ResumeThread: either way the setup ends up running exactly once, and nobody ever kills it
//     or starts a second one. The setup is only touched if its image is exactly the recorded
//     <private folder>\MicrosoftEdgeWebview2Setup.exe and its thread belongs to that PID;
//   - host gone, pid not yet recorded (died between CreateProcess and step 2): the setup is found by
//     that exact image path (a single thread ⇒ still at its start) and treated the same way.
// Nothing is ever terminated on the strength of a record; only the host terminates its own
// never-resumed process (cancel / refused claim), with the handle CreateProcess gave it.
struct Coordination {
    std::wstring marker;   // per-user record
    std::wstring lock;     // per-user lock file (exclusive open)
};
inline Coordination DefaultCoordination() {
    Coordination c;
    wchar_t local[MAX_PATH]; const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n && n < MAX_PATH) { c.marker = std::wstring(local, n) + L"\\browser\\webview2-install.lock"; c.lock = c.marker + L".mutex"; }
    return c;
}
struct Marker {
    DWORD host = 0; std::uint64_t hostCreated = 0;   // launching browser process
    DWORD pid = 0; std::uint64_t created = 0;        // setup process (0 until created)
    DWORD tid = 0; bool resumed = false;             // its main thread; resumed once
    bool cancelled = false;                          // the user withdrew the request before it was resumed
    std::wstring dir;                                // its private folder
};
inline std::uint64_t ProcessCreated(HANDLE process) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(process, &c, &e, &k, &u)) return 0;
    return (std::uint64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}
inline bool ProcessAlive(DWORD pid, std::uint64_t created) {
    if (!pid || !created) return false;
    HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    const bool alive = ProcessCreated(p) == created && WaitForSingleObject(p, 0) == WAIT_TIMEOUT;
    CloseHandle(p);
    return alive;
}
inline bool MarkerAlive(const Marker& m) { return ProcessAlive(m.pid, m.created); }
inline Marker SelfHost() { Marker m; m.host = GetCurrentProcessId(); m.hostCreated = ProcessCreated(GetCurrentProcess()); return m; }
inline bool SameHost(const Marker& a, const Marker& b) { return a.host && a.host == b.host && a.hostCreated == b.hostCreated; }
inline bool ReadMarker(const std::wstring& path, Marker& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buffer[2048]; DWORD got = 0;
    const BOOL ok = ReadFile(f, buffer, sizeof(buffer) - 1, &got, nullptr);
    CloseHandle(f);
    if (!ok) return false;
    buffer[got] = 0;
    std::string text(buffer);
    auto field = [&](const char* key) -> std::string {
        const std::string k = std::string(key) + "=";
        size_t at = text.find(k);
        while (at != std::string::npos && at && text[at - 1] != '\n') at = text.find(k, at + 1);
        if (at == std::string::npos) return {};
        const size_t end = text.find('\n', at);
        return text.substr(at + k.size(), end == std::string::npos ? std::string::npos : end - at - k.size());
    };
    auto number = [&](const char* key, std::uint64_t& v, bool required) {
        const std::string s = field(key);
        if (s.empty()) return !required;
        if (s.size() > 20 || s.find_first_not_of("0123456789") != std::string::npos) return false;
        v = std::strtoull(s.c_str(), nullptr, 10);
        return true;
    };
    std::uint64_t host = 0, hostCreated = 0, pid = 0, created = 0, tid = 0, resumed = 0, cancelled = 0;
    if (!number("host", host, false) || !number("hostCreated", hostCreated, false) || !number("pid", pid, true) ||
        !number("created", created, true) || !number("tid", tid, false) || !number("resumed", resumed, false) ||
        !number("cancelled", cancelled, false)) return false;
    if (!host && !pid) return false;   // identifies nothing
    out.host = DWORD(host); out.hostCreated = hostCreated; out.pid = DWORD(pid); out.created = created; out.tid = DWORD(tid); out.resumed = resumed != 0; out.cancelled = cancelled != 0;
    const std::string dir = field("dir");
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, dir.data(), int(dir.size()), nullptr, 0);
    out.dir.assign(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, dir.data(), int(dir.size()), out.dir.data(), n);
    return true;
}
inline bool WriteMarker(const std::wstring& path, const Marker& m) {
    const size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
    char dir[1024] = {};
    WideCharToMultiByte(CP_UTF8, 0, m.dir.c_str(), -1, dir, sizeof(dir) - 1, nullptr, nullptr);
    char text[1400];
    const int len = std::snprintf(text, sizeof(text), "host=%lu\nhostCreated=%llu\npid=%lu\ncreated=%llu\ntid=%lu\nresumed=%d\ncancelled=%d\ndir=%s\n",
                                  static_cast<unsigned long>(m.host), static_cast<unsigned long long>(m.hostCreated), static_cast<unsigned long>(m.pid),
                                  static_cast<unsigned long long>(m.created), static_cast<unsigned long>(m.tid), m.resumed ? 1 : 0, m.cancelled ? 1 : 0, dir);
    const std::wstring temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = len > 0 && WriteFile(f, text, DWORD(len), &written, nullptr) && written == DWORD(len) && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(temp.c_str()); return false; }
    return true;
}
class CoordinationLock {
public:
    explicit CoordinationLock(const Coordination& c, DWORD waitMs = 5000) {
        if (c.lock.empty()) return;
        const size_t slash = c.lock.find_last_of(L'\\');
        if (slash != std::wstring::npos) CreateDirectoryW(c.lock.substr(0, slash).c_str(), nullptr);
        const ULONGLONG until = GetTickCount64() + waitMs;
        for (;;) {
            handle_ = CreateFileW(c.lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) return;
            if (GetLastError() != ERROR_SHARING_VIOLATION || GetTickCount64() > until) { handle_ = INVALID_HANDLE_VALUE; return; }
            Sleep(15);
        }
    }
    ~CoordinationLock() { if (Held()) CloseHandle(handle_); }
    bool Held() const { return handle_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};
// Exact image of the recorded setup?
inline bool ImageIs(DWORD pid, const std::wstring& expected) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    wchar_t image[MAX_PATH * 2]; DWORD size = MAX_PATH * 2;
    const bool ok = QueryFullProcessImageNameW(p, 0, image, &size) && Lower(std::wstring(image, size)) == Lower(expected);
    CloseHandle(p);
    return ok;
}
// Finds a process of exactly this image path (the host died before recording its PID).
inline bool FindByImage(const std::wstring& image, Marker& m) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
    bool found = false;
    for (BOOL more = Process32FirstW(snap, &e); more && !found; more = Process32NextW(snap, &e)) {
        if (Lower(e.szExeFile) != Lower(FileName()) || !ImageIs(e.th32ProcessID, image)) continue;
        HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
        if (!p) continue;
        m.pid = e.th32ProcessID; m.created = ProcessCreated(p); CloseHandle(p);
        found = m.created != 0;
    }
    CloseHandle(snap);
    if (!found) return false;
    // Its main thread: the only thread of a process still at its start. More than one ⇒ it has run:
    // no thread to resume (resumed = true, it is simply waited for).
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) { m.resumed = true; return true; }
    THREADENTRY32 t{}; t.dwSize = sizeof(t); int threads = 0; DWORD tid = 0;
    for (BOOL more = Thread32First(snap, &t); more; more = Thread32Next(snap, &t)) if (t.th32OwnerProcessID == m.pid) { ++threads; tid = t.th32ThreadID; }
    CloseHandle(snap);
    if (threads == 1) m.tid = tid; else m.resumed = true;
    return true;
}
// Result of evaluating the record (lock held). `adopted` ⇒ a dead host's setup was resumed here;
// `previousSuspend` is what ResumeThread returned (1: the thread was still suspended — with our
// invariant, created SUSPENDED by its host and resumed only through this protocol, that means it
// had not been started; 0: it was already running and the call did nothing). A suspend count alone
// is not a universal proof that no code ran: a program can suspend itself (documented limit).
// `cancelledResidue` ⇒ a withdrawn (cancelled, never resumed) setup is still present, suspended:
// it is never resumed, never counted as an active install, and not killed from here.
struct Settled { DWORD busy = 0; bool adopted = false; DWORD previousSuspend = DWORD(-1); DWORD cancelledResidue = 0; };
inline Settled Settle(const Coordination& c, const Marker& self) {
    Settled s;
    Marker m;
    if (!ReadMarker(c.marker, m)) {
        if (GetFileAttributesW(c.marker.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(c.marker.c_str());   // garbage: not a claim
        return s;
    }
    const bool selfHost = SameHost(m, self);
    if (!selfHost && ProcessAlive(m.host, m.hostCreated)) { s.busy = m.pid ? m.pid : m.host; return s; }
    const std::wstring image = m.dir.empty() ? L"" : m.dir + L"\\" + FileName();
    if (!m.pid && !image.empty() && IsPrivateDirPath(m.dir) && !selfHost) FindByImage(image, m);
    if (m.cancelled && !m.resumed) {
        // Withdrawn by the user before it was resumed (the host decided it under its launch gate:
        // nothing resumes it any more). Never adopted, never an active install.
        if (MarkerAlive(m)) s.cancelledResidue = m.pid;
        if (!m.dir.empty()) RemovePrivateDir(m.dir);   // fails harmlessly while a residue still maps the image
        DeleteFileW(c.marker.c_str());
        return s;
    }
    if (MarkerAlive(m)) {
        s.busy = m.pid;
        if (!m.resumed && m.tid && !selfHost && !image.empty() && ImageIs(m.pid, image)) {
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION, FALSE, m.tid);
            if (t && GetProcessIdOfThread(t) == m.pid) {
                s.previousSuspend = ResumeThread(t);   // 0: already running (no-op); 1: still suspended since its creation, now it runs
                if (s.previousSuspend != DWORD(-1)) { s.adopted = true; m.resumed = true; WriteMarker(c.marker, m); }
            }
            if (t) CloseHandle(t);
        }
        return s;
    }
    if (!m.dir.empty()) RemovePrivateDir(m.dir);   // stale: its setup and host are gone
    DeleteFileW(c.marker.c_str());
    return s;
}
// A live installer launched by any browser process of this user (or being launched by a live
// one), or 0. May adopt a dead host's setup (see Settle). `settled` receives the details.
inline DWORD ActiveInstall(const Coordination& c, Settled* settled = nullptr) {
    if (c.marker.empty()) return 0;
    CoordinationLock lock(c);
    if (!lock.Held()) return 0;
    const Settled s = Settle(c, SelfHost());
    if (settled) *settled = s;
    return s.busy;
}
enum class Claim { Claimed, Busy, Failed };
// Step 1: reserve before CreateProcess (ours = SelfHost() + private folder).
inline Claim ClaimInstall(const Coordination& c, const Marker& ours, DWORD& busyPid) {
    busyPid = 0;
    if (c.marker.empty()) return Claim::Failed;
    CoordinationLock lock(c);
    if (!lock.Held()) return Claim::Failed;
    const Settled s = Settle(c, ours);
    if (s.busy) { busyPid = s.busy; return Claim::Busy; }
    return WriteMarker(c.marker, ours) ? Claim::Claimed : Claim::Failed;
}
// Steps 2 and 3: update our own record (fails if it is no longer ours).
inline bool UpdateInstall(const Coordination& c, const Marker& ours) {
    if (c.marker.empty()) return false;
    CoordinationLock lock(c);
    if (!lock.Held()) return false;
    Marker m;
    if (!ReadMarker(c.marker, m) || !SameHost(m, ours)) return false;
    Marker merged = ours;
    merged.resumed = ours.resumed || m.resumed;
    merged.cancelled = ours.cancelled || m.cancelled;   // a withdrawal is never undone by a later update
    return WriteMarker(c.marker, merged);
}
// The live host records that the user withdrew its request (only if not resumed). False: no
// record of ours, already resumed, or the lock / write failed (the caller logs it).
inline bool MarkCancelled(const Coordination& c) {
    if (c.marker.empty()) return false;
    CoordinationLock lock(c, 1000);   // at close: bounded; the gate has already decided the outcome
    if (!lock.Held()) return false;
    Marker m;
    if (!ReadMarker(c.marker, m) || !SameHost(m, SelfHost()) || m.resumed) return false;
    m.cancelled = true;
    return WriteMarker(c.marker, m);
}
// Step 4: only our own record.
inline void ReleaseInstall(const Coordination& c, const Marker& ours) {
    if (c.marker.empty()) return;
    CoordinationLock lock(c);
    if (!lock.Held()) return;
    Marker m;
    if (ReadMarker(c.marker, m) && SameHost(m, ours) && m.pid == ours.pid) DeleteFileW(c.marker.c_str());
}

// ---------------------------------------------------------------- trust
struct TrustResult {
    enum Status { Ok, Untrusted, Publisher, Unexpected } status = Untrusted;
    DWORD code = 0;
    std::wstring signer;   // signing certificate CN (for the log)
    int root = -1;         // 0: default Microsoft roots, 1: application root (« Microsoft Root Certificate Authority 2011 »)
};
inline std::wstring CertAttr(PCCERT_CONTEXT cert, const char* oid) {
    auto& api = Api::Get();
    wchar_t buffer[256] = {};
    api.NameString(cert, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(oid), buffer, 256);
    return buffer;
}
// OriginalFilename from the RT_VERSION resource of the image behind `file` (the locked handle),
// parsed from a read-only mapping: no path lookup, so no MUI satellite can substitute it. The
// resource section is part of the Authenticode-hashed image.
inline bool ReadOriginalFilename(HANDLE file, std::wstring& out) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 512 || size.QuadPart > LONGLONG(MaxBytes)) return false;
    HANDLE map = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map) return false;
    const BYTE* base = static_cast<const BYTE*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0));
    CloseHandle(map);
    if (!base) return false;
    const size_t n = size_t(size.QuadPart);
    bool found = false;
    auto in = [&](size_t off, size_t len) { return off <= n && len <= n - off; };
    do {
        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || !in(size_t(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS32))) break;
        auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) break;
        const IMAGE_DATA_DIRECTORY* dirs = nullptr;
        if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_RESOURCE) break;
            dirs = nt->OptionalHeader.DataDirectory;
        } else if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            if (!in(size_t(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS64))) break;
            auto nt64 = reinterpret_cast<const IMAGE_NT_HEADERS64*>(nt);
            if (nt64->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_RESOURCE) break;
            dirs = nt64->OptionalHeader.DataDirectory;
        } else break;
        const DWORD resRva = dirs[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
        if (!resRva) break;
        const size_t secOff = size_t(dos->e_lfanew) + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
        const WORD sections = nt->FileHeader.NumberOfSections;
        if (!in(secOff, size_t(sections) * sizeof(IMAGE_SECTION_HEADER))) break;
        auto sec = reinterpret_cast<const IMAGE_SECTION_HEADER*>(base + secOff);
        auto rvaToOff = [&](DWORD rva, size_t& off) {
            for (WORD i = 0; i < sections; ++i) {
                const DWORD va = sec[i].VirtualAddress, raw = sec[i].SizeOfRawData;
                if (rva >= va && rva - va < raw) { off = size_t(sec[i].PointerToRawData) + (rva - va); return in(off, 1); }
            }
            return false;
        };
        size_t root = 0;
        if (!rvaToOff(resRva, root)) break;
        // Resource tree: type RT_VERSION → first name → first language → data entry.
        auto child = [&](size_t dirOff, int wantId, size_t& next, bool& isDir) {
            if (!in(dirOff, sizeof(IMAGE_RESOURCE_DIRECTORY))) return false;
            auto d = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY*>(base + dirOff);
            const size_t count = size_t(d->NumberOfNamedEntries) + d->NumberOfIdEntries;
            const size_t first = dirOff + sizeof(IMAGE_RESOURCE_DIRECTORY);
            if (!in(first, count * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY))) return false;
            auto e = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY_ENTRY*>(base + first);
            for (size_t i = 0; i < count; ++i) {
                if (wantId >= 0 && (e[i].NameIsString || e[i].Id != wantId)) continue;
                isDir = e[i].DataIsDirectory; next = root + e[i].OffsetToDirectory;
                return in(next, 1);
            }
            return false;
        };
        size_t names = 0, langs = 0, dataEntry = 0; bool isDir = false;
        if (!child(root, int(reinterpret_cast<ULONG_PTR>(RT_VERSION)), names, isDir) || !isDir) break;
        if (!child(names, -1, langs, isDir) || !isDir) break;
        if (!child(langs, -1, dataEntry, isDir) || isDir || !in(dataEntry, sizeof(IMAGE_RESOURCE_DATA_ENTRY))) break;
        auto data = reinterpret_cast<const IMAGE_RESOURCE_DATA_ENTRY*>(base + dataEntry);
        size_t vOff = 0;
        if (!rvaToOff(data->OffsetToData, vOff) || !in(vOff, data->Size)) break;
        // VS_VERSIONINFO: UTF-16 blocks { wLength, wValueLength, wType, szKey, pad, value, pad, children }.
        // Search the StringFileInfo string named OriginalFilename (any language table).
        const wchar_t* key = L"OriginalFilename";
        const size_t keyBytes = (wcslen(key) + 1) * sizeof(wchar_t);
        const BYTE* v = base + vOff; const size_t vLen = data->Size;
        for (size_t at = 0; at + 6 + keyBytes <= vLen; at += 2) {
            if (memcmp(v + at + 6, key, keyBytes) != 0) continue;
            WORD blockLen = 0, valueLen = 0; memcpy(&blockLen, v + at, 2); memcpy(&valueLen, v + at + 2, 2);
            size_t value = (at + 6 + keyBytes + 3) & ~size_t(3);
            if (!valueLen || blockLen < 6 || at + blockLen > vLen || value + size_t(valueLen) * 2 > vLen) continue;
            std::wstring s(reinterpret_cast<const wchar_t*>(v + value), valueLen);
            while (!s.empty() && s.back() == L'\0') s.pop_back();
            out = s; found = true;
            break;
        }
    } while (false);
    UnmapViewOfFile(base);
    return found;
}
// `file` is the open, deny-write handle of `path`. Requires: Authenticode valid (whole-chain
// revocation), chain ending in a Microsoft root, signer CN and O = Microsoft Corporation, and
// (requireBootstrapper) the Edge Update setup's version resource from that same image.
inline TrustResult VerifyMicrosoftSignature(HANDLE file, const std::wstring& path, bool requireBootstrapper = true) {
    auto& api = Api::Get();
    TrustResult result;
    if (!api.trust) { result.code = ERROR_PROC_NOT_FOUND; return result; }
    WINTRUST_FILE_INFO info{}; info.cbStruct = sizeof(info); info.pcwszFilePath = path.c_str(); info.hFile = file;
    WINTRUST_DATA data{}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE; data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE; data.pFile = &info; data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = api.VerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    if (status != 0) result.code = static_cast<DWORD>(status);   // « No other value besides zero should be considered a successful return »
    else {
        CRYPT_PROVIDER_DATA* prov = api.ProvData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR* signer = prov ? api.ProvSigner(prov, 0, FALSE, 0) : nullptr;
        if (!signer || !signer->pChainContext || !signer->csCertChain || !signer->pasCertChain || !signer->pasCertChain[0].pCert) {
            result.code = static_cast<DWORD>(TRUST_E_NOSIGNATURE);
        } else {
            bool microsoftRoot = false;
            // dwFlags = 0 alone REJECTS the real bootstrapper (root « Microsoft Root Certificate
            // Authority 2011 »); the documented application-root flag accepts it. Both are Microsoft roots only.
            for (DWORD flags : {DWORD(0), DWORD(MICROSOFT_ROOT_CERT_CHAIN_POLICY_CHECK_APPLICATION_ROOT_FLAG)}) {
                CERT_CHAIN_POLICY_PARA para{}; para.cbSize = sizeof(para); para.dwFlags = flags;
                CERT_CHAIN_POLICY_STATUS policy{}; policy.cbSize = sizeof(policy);
                if (api.ChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT, signer->pChainContext, &para, &policy) && policy.dwError == 0) { microsoftRoot = true; result.root = flags ? 1 : 0; break; }
            }
            PCCERT_CONTEXT leaf = signer->pasCertChain[0].pCert;
            result.signer = CertAttr(leaf, szOID_COMMON_NAME);
            const bool microsoftSubject = result.signer == L"Microsoft Corporation" && CertAttr(leaf, szOID_ORGANIZATION_NAME) == L"Microsoft Corporation";
            std::wstring original;
            if (!microsoftRoot || !microsoftSubject) { result.status = TrustResult::Publisher; result.code = static_cast<DWORD>(CERT_E_UNTRUSTEDROOT); }
            else if (requireBootstrapper && !(ReadOriginalFilename(file, original) && Lower(original) == Lower(ExpectedOriginalName()))) result.status = TrustResult::Unexpected;
            else result.status = TrustResult::Ok;
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    api.VerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return result;
}

// ---------------------------------------------------------------- launch
struct LaunchResult {
    enum Status {
        Exited,      // ran to its end: `code` is its exit code
        Failed,      // not started (`code` = Win32 error)
        Elevation,   // the program requires elevation: never requested
        Timeout,     // started, still running after the wait limit
        Cancelled,   // cancel arrived BEFORE it ran: never resumed (no code executed) and terminated
        Detached,    // cancel arrived after it was resumed: it keeps running on its own
        Busy         // the per-user record is no longer ours: never resumed, terminated
    } status = Failed;
    DWORD code = 0;
    DWORD pid = 0;
    DWORD busyPid = 0;
};
// `created(pid, created, tid)` runs while the new process is still suspended and must record it
// (Claimed) before it may run; `resumed()` runs right after a successful ResumeThread.
// In-process gate between the worker that launches and the host that may cancel: process creation
// and the « last cancel check + ResumeThread » both happen under `mutex`, and so does the host's
// Cancel(). Either the cancel comes first (nothing is created, or the suspended setup is terminated
// with the handle CreateProcessW returned), or the resume does (the setup runs and is never killed).
// No wait, timeout or lock file is involved, so a stalled worker cannot lose a « Quitter ».
struct LaunchGate {
    std::mutex mutex;
    bool cancelled = false, resumed = false, terminated = false;
    HANDLE process = nullptr;   // the suspended setup, owned by the worker, published while not resumed
    DWORD pid = 0;
    // Host side. Returns true when a setup had already been resumed (it keeps running).
    bool Cancel() {
        std::lock_guard<std::mutex> lock(mutex);
        cancelled = true;
        if (process && !resumed && !terminated) {   // created by us, suspended, never resumed: it ran no code of its own
            terminated = TerminateProcess(process, ERROR_CANCELLED) != FALSE;
        }
        return resumed;
    }
};
struct LaunchHooks {
    std::function<Claim(DWORD pid, std::uint64_t created, DWORD tid, DWORD& busyPid)> created;
    std::function<void()> resumed;
    std::shared_ptr<LaunchGate> gate;   // null: no host-side cancel (direct calls in tests)
};
inline LaunchResult RunBootstrapper(const std::wstring& exe, const std::wstring& dir, HANDLE cancel, DWORD timeoutMs, const LaunchHooks& hooks = {}) {
    LaunchResult r;
    auto cancelled = [&] { return cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0; };
    LaunchGate* gate = hooks.gate.get();
    std::unique_lock<std::mutex> held;
    if (gate) held = std::unique_lock<std::mutex>(gate->mutex);
    if (cancelled() || (gate && gate->cancelled)) { r.status = LaunchResult::Cancelled; return r; }
    std::wstring command = L"\"" + exe + L"\" /silent /install";   // i18n-exempt: installer command line
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, dir.c_str(), &si, &pi)) {
        r.code = GetLastError();
        r.status = r.code == ERROR_ELEVATION_REQUIRED ? LaunchResult::Elevation : LaunchResult::Failed;
        return r;
    }
    r.pid = pi.dwProcessId;
    if (gate) { gate->process = pi.hProcess; gate->pid = pi.dwProcessId; held.unlock(); }
    // Created SUSPENDED and not resumed ⇒ it has run no code of its own: terminating it cannot interrupt an install.
    auto abandon = [&](LaunchResult::Status s) {
        if (gate) { std::lock_guard<std::mutex> lock(gate->mutex); gate->process = nullptr; }
        TerminateProcess(pi.hProcess, ERROR_CANCELLED); WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        r.status = s; return r;
    };
    if (hooks.created) {
        const Claim c = hooks.created(pi.dwProcessId, ProcessCreated(pi.hProcess), pi.dwThreadId, r.busyPid);
        if (c == Claim::Busy) return abandon(LaunchResult::Busy);
        if (c == Claim::Failed) { r.code = ERROR_LOCK_VIOLATION; return abandon(LaunchResult::Failed); }
    }
    {
        // Last check and resume, atomic with the host's Cancel().
        std::unique_lock<std::mutex> gateLock;
        if (gate) gateLock = std::unique_lock<std::mutex>(gate->mutex);
        if (cancelled() || (gate && gate->cancelled)) { if (gateLock.owns_lock()) gateLock.unlock(); return abandon(LaunchResult::Cancelled); }
        if (ResumeThread(pi.hThread) == DWORD(-1)) { r.code = GetLastError(); if (gateLock.owns_lock()) gateLock.unlock(); return abandon(LaunchResult::Failed); }
        if (gate) { gate->resumed = true; gate->process = nullptr; }
    }
    CloseHandle(pi.hThread);
    if (hooks.resumed) hooks.resumed();
    HANDLE waits[2] = {pi.hProcess, cancel};
    const DWORD w = WaitForMultipleObjects(cancel ? 2 : 1, waits, FALSE, timeoutMs);
    if (w == WAIT_OBJECT_0) { r.status = LaunchResult::Exited; GetExitCodeProcess(pi.hProcess, &r.code); }
    else if (w == WAIT_OBJECT_0 + 1) r.status = LaunchResult::Detached;   // the installer keeps running
    else if (w == WAIT_TIMEOUT) r.status = LaunchResult::Timeout;
    else { r.status = LaunchResult::Detached; r.code = GetLastError(); }
    CloseHandle(pi.hProcess);
    return r;
}

// ---------------------------------------------------------------- effects
struct Effects {
    std::shared_ptr<Fetch> fetch;
    std::function<TrustResult(HANDLE, const std::wstring&)> verify;
    std::function<LaunchResult(const std::wstring&, const std::wstring&, HANDLE, DWORD, const LaunchHooks&)> launch;
    Coordination coordination;   // empty marker ⇒ no launch (fail closed)
    std::shared_ptr<LaunchGate> gate;   // set by Installer: the host's Cancel() decides atomically with the resume
    std::wstring url;            // "" = BootstrapperUrl()
    std::wstring tempParent;     // "" = user temp folder
    DWORD timeoutMs = InstallTimeoutMs;
};
inline Effects ProductionEffects() {
    Effects e;
    e.fetch = std::make_shared<WinHttpFetch>();
    e.verify = [](HANDLE f, const std::wstring& p) { return VerifyMicrosoftSignature(f, p, true); };
    e.launch = RunBootstrapper;
    e.coordination = DefaultCoordination();
    return e;
}

// ---------------------------------------------------------------- one install, on a worker
using WebView2Runtime::InstallReport;
// What the worker is really doing (the UI's phase arrives later through PostMessage).
enum class Stage { Idle, Downloading, Verifying, Launching, Running, Done };
inline const wchar_t* StageName(Stage s) {
    switch (s) {
    case Stage::Idle: return L"idle"; case Stage::Downloading: return L"downloading"; case Stage::Verifying: return L"verifying";
    case Stage::Launching: return L"launching"; case Stage::Running: return L"installer-running"; case Stage::Done: return L"done";
    }
    return L"?";
}
inline InstallReport RunInstall(const Effects& e, const std::atomic<bool>& cancel, HANDLE cancelEvent,
                                const std::function<void(WebView2Runtime::Phase, std::uint64_t, std::uint64_t)>& progress,
                                std::atomic<int>* stage = nullptr, const std::function<void(const std::wstring&)>& onDir = nullptr) {
    using O = InstallReport::Outcome;
    auto set = [&](Stage s) { if (stage) stage->store(int(s)); };
    InstallReport report;
    if (DWORD busy = ActiveInstall(e.coordination)) { report.outcome = O::Busy; report.code = busy; return report; }
    set(Stage::Downloading);
    PrivateDir dir;
    if (DWORD err = dir.Create(e.tempParent)) { report.outcome = O::Storage; report.code = err; return report; }
    if (onDir) onDir(dir.Name());
    // Step 1: reserve the per-user record (this host's identity + its private folder) before
    // downloading, so another install waits for this one and a crash at any later point leaves a
    // record that names exactly what to clean or to wait for.
    Marker ours = SelfHost(); ours.dir = dir.Path();
    {
        DWORD busyPid = 0;
        const Claim pre = ClaimInstall(e.coordination, ours, busyPid);
        if (pre == Claim::Busy) { report.outcome = O::Busy; report.code = busyPid; return report; }
        if (pre == Claim::Failed) { report.outcome = O::Launch; report.code = ERROR_LOCK_VIOLATION; return report; }   // no storage for the claim: never launched
    }
    struct Hold {   // step 4 on every exit, unless the setup still runs (then its record must stay)
        const Coordination& c; Marker& m; bool keep = false;
        ~Hold() { if (!keep) ReleaseInstall(c, m); }
    } hold{e.coordination, ours};
    const std::wstring path = dir.Path() + L"\\" + FileName();
    {
        HANDLE out = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) { report.outcome = O::Storage; report.code = GetLastError(); return report; }
        bool writeFailed = false;
        auto sink = [&](const void* p, std::size_t n) {
            DWORD written = 0;
            if (!WriteFile(out, p, DWORD(n), &written, nullptr) || written != n) { writeFailed = true; return false; }
            return true;
        };
        auto onProgress = [&](std::uint64_t got, std::uint64_t total) { progress(WebView2Runtime::Phase::Downloading, got, total); };
        FetchResult f = e.fetch ? e.fetch->Get(e.url.empty() ? BootstrapperUrl() : e.url, MaxBytes, sink, onProgress, cancel) : FetchResult{};
        const bool flushed = FlushFileBuffers(out) != FALSE;
        CloseHandle(out);
        if (writeFailed || (f.status == FetchResult::Ok && !flushed)) { report.outcome = O::Storage; report.code = ERROR_WRITE_FAULT; return report; }
        switch (f.status) {
        case FetchResult::Ok: break;
        case FetchResult::Cancelled: report.outcome = O::Cancelled; return report;
        case FetchResult::Http: report.outcome = O::Http; report.code = f.code; return report;
        case FetchResult::Rejected: report.outcome = O::Rejected; report.code = f.code; return report;
        case FetchResult::TooLarge: report.outcome = O::TooLarge; return report;
        case FetchResult::TimedOut: report.outcome = O::DownloadTimeout; report.code = f.code; return report;
        case FetchResult::Network: report.outcome = O::Network; report.code = f.code; return report;
        }
    }
    if (cancel.load()) { report.outcome = O::Cancelled; return report; }
    // Deny write and delete to everyone else until the installer has exited.
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (locked == INVALID_HANDLE_VALUE) { report.outcome = O::Storage; report.code = GetLastError(); return report; }
    set(Stage::Verifying);
    progress(WebView2Runtime::Phase::Verifying, 0, 0);
    TrustResult t = e.verify ? e.verify(locked, path) : TrustResult{};
    report.signer = t.signer;
    if (t.status != TrustResult::Ok) {
        CloseHandle(locked);
        report.outcome = t.status == TrustResult::Publisher ? O::Publisher : t.status == TrustResult::Unexpected ? O::Unexpected : O::Trust;
        report.code = t.code;
        return report;
    }
    if (cancel.load()) { CloseHandle(locked); report.outcome = O::Cancelled; return report; }
    set(Stage::Launching);
    LaunchHooks hooks;
    hooks.gate = e.gate;
    hooks.created = [&](DWORD pid, std::uint64_t created, DWORD tid, DWORD& busyPid) {   // step 2, still suspended
        ours.pid = pid; ours.created = created; ours.tid = tid; busyPid = 0;
        return UpdateInstall(e.coordination, ours) ? Claim::Claimed : Claim::Failed;
    };
    hooks.resumed = [&] { ours.resumed = true; UpdateInstall(e.coordination, ours); };   // step 3 (flags on disk are merged, never cleared)
    // Phase shown when it is about to run (the claim and the last cancel check come first).
    progress(WebView2Runtime::Phase::Installing, 0, 0);
    LaunchResult l = e.launch ? e.launch(path, dir.Path(), cancelEvent, e.timeoutMs, hooks) : LaunchResult{};
    CloseHandle(locked);
    report.pid = l.pid;
    switch (l.status) {
    case LaunchResult::Exited: report.outcome = O::Finished; report.code = l.code; break;
    case LaunchResult::Elevation: report.outcome = O::Elevation; report.code = l.code; break;
    case LaunchResult::Failed: report.outcome = O::Launch; report.code = l.code; break;
    case LaunchResult::Timeout: report.outcome = O::Timeout; report.installerRunning = true; break;
    case LaunchResult::Cancelled: report.outcome = O::Cancelled; break;
    case LaunchResult::Detached: report.outcome = O::Detached; report.installerRunning = true; break;
    case LaunchResult::Busy: report.outcome = O::Busy; report.code = l.busyPid; break;
    }
    if (report.installerRunning) {
        hold.keep = true;
        set(Stage::Running);
        report.residue = dir.Name();
        dir.Release();   // in use by the running installer; recorded in the marker, removed by the next claim after it exits
    } else {
        report.cleanupIncomplete = !dir.Remove();
        if (report.cleanupIncomplete) report.residue = ours.dir.substr(ours.dir.find_last_of(L'\\') + 1);
    }
    set(Stage::Done);
    return report;
}

// Single-flight worker. Events are drained on the UI thread after `notify` (a PostMessage);
// `final` (thread-safe log) receives the real outcome even after Shutdown stopped the UI.
class Installer {
public:
    struct Event { bool done = false; WebView2Runtime::Phase phase = WebView2Runtime::Phase::Downloading; std::uint64_t bytes = 0, total = 0; InstallReport report; };
    struct ShutdownState {
        Stage stage = Stage::Idle; bool detached = false; std::wstring dir;
        bool alreadyResumed = false;    // the setup was resumed before the cancel: it keeps running (never killed)
        bool terminatedSuspended = false;   // a created, never-resumed setup was terminated by the cancel
        bool withdrawalRecorded = false;    // `cancelled=1` written to our record (no record / resumed: false)
    };
    Installer() = default;
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;
    ~Installer() { Shutdown(0); }

    bool Running() const { return state_ && !state_->finished.load(); }
    bool Start(Effects effects, std::function<void()> notify, std::function<void(const InstallReport&)> final = nullptr) {
        if (Running()) return false;
        if (thread_.joinable()) thread_.join();
        auto s = std::make_shared<State>();
        s->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!s->cancelEvent) return false;
        s->fetch = effects.fetch;
        s->gate = std::make_shared<LaunchGate>();
        effects.gate = s->gate;
        s->coordination = effects.coordination;
        s->notify = std::move(notify);
        state_ = s;
        try {
            thread_ = std::thread([s, effects = std::move(effects), final = std::move(final)] {
                std::uint64_t lastPercent = ~0ull;
                auto push = [&](Event ev) {
                    std::function<void()> notify;
                    { std::lock_guard<std::mutex> lock(s->mutex); s->events.push_back(ev); notify = s->notify; }
                    if (notify) notify();
                };
                auto progress = [&](WebView2Runtime::Phase phase, std::uint64_t bytes, std::uint64_t total) {
                    if (phase == WebView2Runtime::Phase::Downloading) {
                        const std::uint64_t percent = total ? bytes * 100 / total : bytes >> 18;
                        if (percent == lastPercent) return;
                        lastPercent = percent;
                    }
                    Event ev; ev.phase = phase; ev.bytes = bytes; ev.total = total; push(ev);
                };
                Event done; done.done = true;
                done.report = RunInstall(effects, s->cancel, s->cancelEvent, progress, &s->stage,
                                         [&](const std::wstring& d) { std::lock_guard<std::mutex> lock(s->mutex); s->dir = d; });
                if (final) final(done.report);
                s->finished = true;
                push(done);
            });
        } catch (...) {
            state_.reset();
            return false;
        }
        return true;
    }
    std::vector<Event> Take() {
        if (!state_) return {};
        std::lock_guard<std::mutex> lock(state_->mutex);
        std::vector<Event> out; out.swap(state_->events);
        return out;
    }
    Stage CurrentStage() const { return state_ ? Stage(state_->stage.load()) : Stage::Idle; }
    void Cancel() {
        if (!state_) return;
        state_->cancel = true;
        if (state_->gate) state_->gate->Cancel();
        SetEvent(state_->cancelEvent);
        if (state_->fetch) state_->fetch->Abort();
    }
    // Closing: cancel, wait up to `waitMs`, then let the worker finish on its own (it owns its
    // state; PostMessage to a destroyed HWND fails harmlessly). Returns what was really going on.
    ShutdownState Shutdown(DWORD waitMs) {
        ShutdownState out;
        if (!state_) return out;
        auto s = state_;
        { std::lock_guard<std::mutex> lock(s->mutex); s->notify = nullptr; }
        out.stage = Stage(s->stage.load());
        Cancel();
        // The withdrawal is decided (gate) and, while this host lives, recorded: no later check may
        // resume this request, whatever the worker does after this process has gone.
        if (s->gate) {
            std::lock_guard<std::mutex> lock(s->gate->mutex);
            out.alreadyResumed = s->gate->resumed;
            out.terminatedSuspended = s->gate->terminated;
        }
        if (!out.alreadyResumed && out.stage != Stage::Idle && out.stage != Stage::Done) out.withdrawalRecorded = MarkCancelled(s->coordination);
        const ULONGLONG until = GetTickCount64() + waitMs;
        while (!s->finished.load() && GetTickCount64() < until) Sleep(10);
        if (thread_.joinable()) { if (s->finished.load()) thread_.join(); else { thread_.detach(); out.detached = true; } }
        { std::lock_guard<std::mutex> lock(s->mutex); out.dir = s->dir; }
        // out.stage stays the stage at the moment of the close (what the user interrupted).
        state_.reset();
        return out;
    }
private:
    struct State {
        std::atomic<bool> cancel{false}, finished{false};
        std::atomic<int> stage{int(Stage::Idle)};
        HANDLE cancelEvent = nullptr;
        std::shared_ptr<Fetch> fetch;
        std::shared_ptr<LaunchGate> gate;
        Coordination coordination;
        std::function<void()> notify;
        std::mutex mutex;
        std::vector<Event> events;
        std::wstring dir;
        ~State() { if (cancelEvent) CloseHandle(cancelEvent); }
    };
    std::shared_ptr<State> state_;
    std::thread thread_;
};

}  // namespace WebView2Install
