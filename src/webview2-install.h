#pragma once
// Evergreen WebView2 Runtime install through Microsoft's bootstrapper, started only by the user
// from the startup recovery dialog (src/webview2-runtime.h, design/webview2-startup-recovery.md).
//
//   1. GET https://go.microsoft.com/fwlink/p/?LinkId=2124703 — the « Get the Link » address of
//      https://developer.microsoft.com/microsoft-edge/webview2/ (Evergreen Bootstrapper). HTTPS
//      only (TLS 1.2/1.3, system certificate validation untouched), redirects followed manually
//      and only to https://microsoft.com or https://*.microsoft.com on port 443, ≤ 16 MiB.
//   2. Written into a fresh directory with a random name under the user's temp folder, DACL
//      protected (current user + SYSTEM); re-opened read-only, deny-write/deny-delete, and that
//      handle stays open through verification and execution.
//   3. WinVerifyTrust (WINTRUST_ACTION_GENERIC_VERIFY_V2, whole-chain revocation) on that handle,
//      then the signer chain must end in a Microsoft root (CertVerifyCertificateChainPolicy,
//      CERT_CHAIN_POLICY_MICROSOFT_ROOT) AND the signing certificate must be CN/O « Microsoft
//      Corporation », AND the signed version resource must name MicrosoftEdgeUpdateSetup.exe.
//   4. CreateProcessW(<file>, "\"<file>\" /silent /install") — the documented command; no shell,
//      no inherited handles, no elevation (an unelevated run installs per-user, distribution doc).
//   5. The file and directory are removed afterwards (best effort, logged by the host).
//
// Link-free: winhttp/wintrust/crypt32/version are resolved at run time from System32 only, so this
// header compiles into every build of main.cpp (stamped, preview, unstamped fixtures) without new
// link flags, and the updater's GitHub transport (update-core) is not touched.
// Tests replace the network, trust and process steps through `Effects`; nothing in production
// reads a variable or flag that could relax the URL, TLS or signature checks.
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <sddl.h>

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
class Api {
public:
    static Api& Get() { static Api api; return api; }
    bool http = false, trust = false, version = false;
    decltype(&::WinHttpOpen) Open = nullptr; decltype(&::WinHttpSetTimeouts) SetTimeouts = nullptr;
    decltype(&::WinHttpSetOption) SetOption = nullptr; decltype(&::WinHttpConnect) Connect = nullptr;
    decltype(&::WinHttpOpenRequest) OpenRequest = nullptr; decltype(&::WinHttpSendRequest) SendRequest = nullptr;
    decltype(&::WinHttpReceiveResponse) ReceiveResponse = nullptr; decltype(&::WinHttpQueryHeaders) QueryHeaders = nullptr;
    decltype(&::WinHttpReadData) ReadData = nullptr; decltype(&::WinHttpCloseHandle) CloseHandle = nullptr;
    decltype(&::WinVerifyTrust) VerifyTrust = nullptr; decltype(&::WTHelperProvDataFromStateData) ProvData = nullptr;
    decltype(&::WTHelperGetProvSignerFromChain) ProvSigner = nullptr;
    decltype(&::CertVerifyCertificateChainPolicy) ChainPolicy = nullptr; decltype(&::CertGetNameStringW) NameString = nullptr;
    decltype(&::GetFileVersionInfoSizeW) VersionSize = nullptr; decltype(&::GetFileVersionInfoW) VersionInfo = nullptr;
    decltype(&::VerQueryValueW) VersionQuery = nullptr;
private:
    Api() {
        auto load = [](const wchar_t* dll) { return LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); };
        HMODULE h = load(L"winhttp.dll"), t = load(L"wintrust.dll"), c = load(L"crypt32.dll"), v = load(L"version.dll");
        http = Resolve(h, "WinHttpOpen", Open) & Resolve(h, "WinHttpSetTimeouts", SetTimeouts) & Resolve(h, "WinHttpSetOption", SetOption) &
               Resolve(h, "WinHttpConnect", Connect) & Resolve(h, "WinHttpOpenRequest", OpenRequest) & Resolve(h, "WinHttpSendRequest", SendRequest) &
               Resolve(h, "WinHttpReceiveResponse", ReceiveResponse) & Resolve(h, "WinHttpQueryHeaders", QueryHeaders) &
               Resolve(h, "WinHttpReadData", ReadData) & Resolve(h, "WinHttpCloseHandle", CloseHandle);
        trust = Resolve(t, "WinVerifyTrust", VerifyTrust) & Resolve(t, "WTHelperProvDataFromStateData", ProvData) &
                Resolve(t, "WTHelperGetProvSignerFromChain", ProvSigner) & Resolve(c, "CertVerifyCertificateChainPolicy", ChainPolicy) &
                Resolve(c, "CertGetNameStringW", NameString);
        version = Resolve(v, "GetFileVersionInfoSizeW", VersionSize) & Resolve(v, "GetFileVersionInfoW", VersionInfo) & Resolve(v, "VerQueryValueW", VersionQuery);
    }
};

// ---------------------------------------------------------------- fetch
struct FetchResult {
    enum Status { Ok, Network, Http, Rejected, TooLarge, Cancelled } status = Network;
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
// 10/10/15/20 s timeouts, TLS 1.2 (+1.3), no cookies, no authentication, ≤ 6 manual redirects.
class WinHttpFetch : public Fetch {
public:
    WinHttpFetch() {
        auto& api = Api::Get();
        if (!api.http) return;
        session_ = api.Open(L"browser-webview2-setup/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) session_ = api.Open(L"browser-webview2-setup/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) return;
        api.SetTimeouts(session_, 10000, 10000, 15000, 20000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!api.SetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            api.SetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
    }
    ~WinHttpFetch() override { if (session_) Api::Get().CloseHandle(session_); }
    void Abort() override {
        std::lock_guard<std::mutex> lock(mutex_);
        aborted_ = true;
        if (request_) { Api::Get().CloseHandle(request_); request_ = nullptr; }
    }
    FetchResult Get(const std::wstring& startUrl, std::uint64_t maxBytes, const std::function<bool(const void*, std::size_t)>& sink,
                    const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>& cancel) override {
        auto& api = Api::Get();
        FetchResult r;
        if (!session_) { r.code = ERROR_PROC_NOT_FOUND; return r; }
        std::wstring url = startUrl;
        for (int hop = 0; hop < 6; ++hop) {
            if (cancel.load()) return r.status = FetchResult::Cancelled, r;
            HttpsUrl u;
            if (!ParseHttpsUrl(url, u) || !IsMicrosoftHost(u.host)) return r.status = FetchResult::Rejected, r;
            HINTERNET connect = api.Connect(session_, u.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!connect) return r.code = GetLastError(), r;
            HINTERNET request = api.OpenRequest(connect, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (!request) { r.code = GetLastError(); api.CloseHandle(connect); return r; }
            DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
            api.SetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (aborted_ || cancel.load()) { api.CloseHandle(request); api.CloseHandle(connect); return r.status = FetchResult::Cancelled, r; }
                request_ = request;
            }
            auto finish = [&](FetchResult res) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (request_) api.CloseHandle(request_);
                    request_ = nullptr;
                    if (aborted_ || cancel.load()) res.status = FetchResult::Cancelled;
                }
                api.CloseHandle(connect);
                return res;
            };
            if (!api.SendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !api.ReceiveResponse(request, nullptr)) {
                r.code = GetLastError();   // includes ERROR_WINHTTP_SECURE_FAILURE: TLS is never relaxed
                return finish(r);
            }
            DWORD status = 0, size = sizeof(status);
            api.QueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                wchar_t location[4096]; DWORD bytes = sizeof(location);
                if (!api.QueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location, &bytes, WINHTTP_NO_HEADER_INDEX)) {
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
            if (api.QueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &textBytes, WINHTTP_NO_HEADER_INDEX))
                length = std::wcstoull(text, nullptr, 10);
            if (length > maxBytes) { r.status = FetchResult::TooLarge; return finish(r); }
            std::vector<char> buffer(64 * 1024);
            std::uint64_t total = 0;
            for (;;) {
                if (cancel.load()) { r.status = FetchResult::Cancelled; return finish(r); }
                DWORD got = 0;
                if (!api.ReadData(request, buffer.data(), DWORD(buffer.size()), &got)) { r.code = GetLastError(); return finish(r); }
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
    HINTERNET session_ = nullptr;
    std::mutex mutex_;
    HINTERNET request_ = nullptr;
    bool aborted_ = false;
};

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
    // Removes only the files this installer wrote (by name) and the directory itself.
    bool Remove() {
        if (path_.empty()) return true;
        DeleteFileW((path_ + L"\\" + FileName()).c_str());
        const bool ok = RemoveDirectoryW(path_.c_str()) != FALSE;
        if (ok) path_.clear();
        return ok;
    }
    void Release() { path_.clear(); }   // leave it (installer still running)
private:
    // RtlGenRandom (advapi32 SystemFunction036), the CSPRNG behind rand_s.
    static bool Random(void* out, ULONG size) {
        using Gen = BOOLEAN(WINAPI*)(PVOID, ULONG);
        static Gen gen = [] { Gen g = nullptr; Resolve(LoadLibraryExW(L"advapi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "SystemFunction036", g); return g; }();
        return gen && gen(out, size);
    }
    static DWORD UserSid(std::wstring& out) {
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
    std::wstring path_;
};

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
// The signed version resource of the verified file must be Microsoft Edge Update's setup.
inline bool ExpectedProduct(const std::wstring& path) {
    auto& api = Api::Get();
    if (!api.version) return false;
    DWORD ignored = 0, size = api.VersionSize(path.c_str(), &ignored);
    if (!size) return false;
    std::vector<BYTE> data(size);
    if (!api.VersionInfo(path.c_str(), 0, size, data.data())) return false;
    struct Lang { WORD language, codepage; }* langs = nullptr; UINT langBytes = 0;
    if (!api.VersionQuery(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &langBytes) || !langBytes) return false;
    for (UINT i = 0; i < langBytes / sizeof(Lang); ++i) {
        wchar_t key[64]; swprintf(key, 64, L"\\StringFileInfo\\%04x%04x\\OriginalFilename", langs[i].language, langs[i].codepage);
        wchar_t* value = nullptr; UINT chars = 0;
        if (api.VersionQuery(data.data(), key, reinterpret_cast<void**>(&value), &chars) && value && Lower(value) == Lower(ExpectedOriginalName())) return true;
    }
    return false;
}
// `file` is the open, deny-write handle of `path`. Requires: Authenticode valid (whole-chain
// revocation), chain ending in a Microsoft root, signer CN and O = Microsoft Corporation.
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
            for (DWORD flags : {DWORD(0), DWORD(MICROSOFT_ROOT_CERT_CHAIN_POLICY_CHECK_APPLICATION_ROOT_FLAG)}) {
                CERT_CHAIN_POLICY_PARA para{}; para.cbSize = sizeof(para); para.dwFlags = flags;
                CERT_CHAIN_POLICY_STATUS policy{}; policy.cbSize = sizeof(policy);
                if (api.ChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT, signer->pChainContext, &para, &policy) && policy.dwError == 0) { microsoftRoot = true; result.root = flags ? 1 : 0; break; }
            }
            PCCERT_CONTEXT leaf = signer->pasCertChain[0].pCert;
            result.signer = CertAttr(leaf, szOID_COMMON_NAME);
            const bool microsoftSubject = result.signer == L"Microsoft Corporation" && CertAttr(leaf, szOID_ORGANIZATION_NAME) == L"Microsoft Corporation";
            if (!microsoftRoot || !microsoftSubject) { result.status = TrustResult::Publisher; result.code = static_cast<DWORD>(CERT_E_UNTRUSTEDROOT); }
            else if (requireBootstrapper && !ExpectedProduct(path)) result.status = TrustResult::Unexpected;
            else result.status = TrustResult::Ok;
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    api.VerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return result;
}

// ---------------------------------------------------------------- launch
struct LaunchResult {
    enum Status { Exited, Failed, Elevation, Timeout, Cancelled } status = Failed;
    DWORD code = 0;   // exit code or Win32 error
};
inline LaunchResult RunBootstrapper(const std::wstring& exe, const std::wstring& dir, HANDLE cancel, DWORD timeoutMs) {
    LaunchResult r;
    std::wstring command = L"\"" + exe + L"\" /silent /install";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
        r.code = GetLastError();
        r.status = r.code == ERROR_ELEVATION_REQUIRED ? LaunchResult::Elevation : LaunchResult::Failed;
        return r;
    }
    CloseHandle(pi.hThread);
    HANDLE waits[2] = {pi.hProcess, cancel};
    const DWORD w = WaitForMultipleObjects(cancel ? 2 : 1, waits, FALSE, timeoutMs);
    if (w == WAIT_OBJECT_0) { r.status = LaunchResult::Exited; GetExitCodeProcess(pi.hProcess, &r.code); }
    else if (w == WAIT_OBJECT_0 + 1) r.status = LaunchResult::Cancelled;   // the installer keeps running on its own
    else if (w == WAIT_TIMEOUT) r.status = LaunchResult::Timeout;
    else { r.status = LaunchResult::Failed; r.code = GetLastError(); }
    CloseHandle(pi.hProcess);
    return r;
}

// ---------------------------------------------------------------- effects
struct Effects {
    std::shared_ptr<Fetch> fetch;
    std::function<TrustResult(HANDLE, const std::wstring&)> verify;
    std::function<LaunchResult(const std::wstring&, const std::wstring&, HANDLE, DWORD)> launch;
    std::wstring url;          // "" = BootstrapperUrl()
    std::wstring tempParent;   // "" = user temp folder
    DWORD timeoutMs = InstallTimeoutMs;
};
inline Effects ProductionEffects() {
    Effects e;
    e.fetch = std::make_shared<WinHttpFetch>();
    e.verify = [](HANDLE f, const std::wstring& p) { return VerifyMicrosoftSignature(f, p, true); };
    e.launch = RunBootstrapper;
    return e;
}

// ---------------------------------------------------------------- one install, on a worker
using WebView2Runtime::InstallReport;
inline InstallReport RunInstall(const Effects& e, const std::atomic<bool>& cancel, HANDLE cancelEvent,
                                const std::function<void(WebView2Runtime::Phase, std::uint64_t, std::uint64_t)>& progress) {
    using O = InstallReport::Outcome;
    InstallReport report;
    PrivateDir dir;
    if (DWORD err = dir.Create(e.tempParent)) { report.outcome = O::Storage; report.code = err; return report; }
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
        case FetchResult::Rejected: report.outcome = O::Rejected; return report;
        case FetchResult::TooLarge: report.outcome = O::TooLarge; return report;
        case FetchResult::Network: report.outcome = O::Network; report.code = f.code; return report;
        }
    }
    if (cancel.load()) { report.outcome = O::Cancelled; return report; }
    // Deny write and delete to everyone else until the installer has exited.
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (locked == INVALID_HANDLE_VALUE) { report.outcome = O::Storage; report.code = GetLastError(); return report; }
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
    progress(WebView2Runtime::Phase::Installing, 0, 0);
    LaunchResult l = e.launch ? e.launch(path, dir.Path(), cancelEvent, e.timeoutMs) : LaunchResult{};
    CloseHandle(locked);
    switch (l.status) {
    case LaunchResult::Exited: report.outcome = O::Finished; report.code = l.code; break;
    case LaunchResult::Elevation: report.outcome = O::Elevation; report.code = l.code; break;
    case LaunchResult::Failed: report.outcome = O::Launch; report.code = l.code; break;
    case LaunchResult::Timeout: report.outcome = O::Timeout; report.installerRunning = true; break;
    case LaunchResult::Cancelled: report.outcome = O::Cancelled; report.installerRunning = true; break;
    }
    if (report.installerRunning) dir.Release();   // in use by the running installer: left in place
    else report.cleanupIncomplete = !dir.Remove();
    return report;
}

// Single-flight worker. Events are drained on the UI thread after `notify` (a PostMessage).
class Installer {
public:
    struct Event { bool done = false; WebView2Runtime::Phase phase = WebView2Runtime::Phase::Downloading; std::uint64_t bytes = 0, total = 0; InstallReport report; };
    Installer() = default;
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;
    ~Installer() { Shutdown(0); }

    bool Running() const { return state_ && !state_->finished.load(); }
    bool Start(Effects effects, std::function<void()> notify) {
        if (Running()) return false;
        if (thread_.joinable()) thread_.join();
        auto s = std::make_shared<State>();
        s->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!s->cancelEvent) return false;
        s->fetch = effects.fetch;
        s->notify = std::move(notify);
        state_ = s;
        try {
            thread_ = std::thread([s, effects = std::move(effects)] {
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
                done.report = RunInstall(effects, s->cancel, s->cancelEvent, progress);
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
    void Cancel() {
        if (!state_) return;
        state_->cancel = true;
        SetEvent(state_->cancelEvent);
        if (state_->fetch) state_->fetch->Abort();
    }
    // Closing: cancel, wait up to `waitMs`, then let the worker finish on its own (it owns its state
    // and only posts to a window that may be gone — PostMessage to a destroyed HWND fails harmlessly).
    void Shutdown(DWORD waitMs) {
        if (!state_) return;
        auto s = state_;
        { std::lock_guard<std::mutex> lock(s->mutex); s->notify = nullptr; }
        Cancel();
        const ULONGLONG until = GetTickCount64() + waitMs;
        while (!s->finished.load() && GetTickCount64() < until) Sleep(10);
        if (thread_.joinable()) { if (s->finished.load()) thread_.join(); else thread_.detach(); }
        state_.reset();
    }
private:
    struct State {
        std::atomic<bool> cancel{false}, finished{false};
        HANDLE cancelEvent = nullptr;
        std::shared_ptr<Fetch> fetch;
        std::function<void()> notify;
        std::mutex mutex;
        std::vector<Event> events;
        ~State() { if (cancelEvent) CloseHandle(cancelEvent); }
    };
    std::shared_ptr<State> state_;
    std::thread thread_;
};

}  // namespace WebView2Install
