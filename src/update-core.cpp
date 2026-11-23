#include "update-core.h"
#include "i18n.h"   // messages shown in the About dialog and the update toast

#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <cwchar>
#include <vector>

namespace UpdateCore {
namespace UP = UpdatePackage;
using BrowserUpdate::Action;
using BrowserUpdate::Phase;

namespace {

constexpr std::int64_t SuccessTtl = 6 * 3600;
constexpr std::int64_t FailureBase = 3600, FailureMax = 24 * 3600;
constexpr std::uint64_t ProgressIntervalMs = 100;

std::wstring Lower(std::wstring s) {
    CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}
std::wstring Parent(const std::wstring& p) {
    auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : p.substr(0, pos);
}
std::wstring Leaf(const std::wstring& p) {
    auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}
std::wstring FullPath(const std::wstring& p) {
    if (p.empty()) return L"";
    wchar_t buffer[4096];
    DWORD n = GetFullPathNameW(p.c_str(), 4096, buffer, nullptr);
    if (!n || n >= 4096) return L"";
    std::wstring s(buffer, n);
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    return s;
}
std::string RandomHex(std::size_t bytes) {
    std::vector<std::uint8_t> b(bytes);
    if (BCryptGenRandom(nullptr, b.data(), ULONG(b.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        auto t = GetTickCount64() ^ (std::uint64_t(GetCurrentProcessId()) << 32);
        for (std::size_t i = 0; i < bytes; ++i) b[i] = std::uint8_t(t >> ((i % 8) * 8));
    }
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto x : b) {
        s += d[x >> 4];
        s += d[x & 15];
    }
    return s;
}
std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::size_t stop = end;
        if (stop > start && text[stop - 1] == '\r') --stop;  // tolerate CRLF records
        out.push_back(text.substr(start, stop - start));
        start = end + 1;
    }
    return out;
}
// key/value from "key value" lines.
std::string Field(const std::vector<std::string>& lines, const std::string& key) {
    for (const auto& l : lines)
        if (l.size() > key.size() && l.compare(0, key.size(), key) == 0 && l[key.size()] == ' ') return l.substr(key.size() + 1);
    return "";
}
std::wstring W(const std::string& s) { return UP::Widen(s); }

std::wstring DefaultRepository() {
#ifdef BROWSER_UPDATE_REPO
    return BROWSER_UPDATE_REPO;
#else
    return L"VALXXII/browser-releases";
#endif
}

}  // namespace

// ---------------------------------------------------------------- data paths

std::wstring DefaultInstallRoot() {
    wchar_t buffer[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH * 2);
    if (!n || n >= MAX_PATH * 2) return L"";
    return std::wstring(buffer, n) + L"\\browser";
}

DataPaths ResolveDataPaths(const std::wstring& exePath, const std::wstring& installRoot) {
    DataPaths p;
    std::wstring exe = FullPath(exePath), dir = Parent(exe);
    std::wstring root = FullPath(installRoot.empty() ? DefaultInstallRoot() : installRoot);
    std::wstring versions = Parent(dir);
    UP::Version v;
    if (!root.empty() && Lower(Leaf(exe)) == L"browser.exe" && Lower(versions) == Lower(root + L"\\versions") &&
        UP::ParseVersion(Leaf(dir), v)) {
        p.installed = true;
        p.dataDir = root + L"\\data";
        p.profileDir = p.dataDir + L"\\profile";
        p.sessionFile = p.dataDir + L"\\session";
        p.downloadsFile = p.dataDir + L"\\downloads";
        p.logFile = p.dataDir + L"\\browser.log";
    } else {
        p.dataDir = dir;
        p.profileDir = dir + L"\\profile";
        p.sessionFile = dir + L"\\session";
        p.downloadsFile = dir + L"\\downloads";
        p.logFile = dir + L"\\prototype.log";
    }
    return p;
}

std::wstring PortableForwardTarget(const std::wstring& exePath, const std::wstring& installRoot) {
    const std::wstring exe = FullPath(exePath);
    const std::wstring root = FullPath(installRoot.empty() ? DefaultInstallRoot() : installRoot);
    if (exe.empty() || root.empty() || exe.size() < 3 || exe[1] != L':') return L"";
    if (ResolveDataPaths(exe, root).installed) return L"";       // installed kind never forwards (no loop)
    if (UP::IsUnder(exe, root) || Lower(Parent(exe)) == Lower(root)) return L"";  // launcher/anything inside root
    const std::wstring data = root + L"\\data", versions = root + L"\\versions";
    for (const std::wstring& dir : {root, data, versions})
        if (!UP::IsDirectory(dir) || UP::IsReparsePoint(dir)) return L"";
    // The successful first install recorded where the portable data came from: it must be this exe's folder.
    std::string from;
    if (!UP::ReadSmallFile(data + L"\\migrated-from.txt", 4096, from)) return L"";
    while (!from.empty() && (from.back() == '\n' || from.back() == '\r')) from.pop_back();
    if (from.empty() || Lower(FullPath(UP::Widen(from))) != Lower(Parent(exe))) return L"";
    std::string current;
    if (!UP::ReadSmallFile(root + L"\\current", 64, current)) return L"";
    while (!current.empty() && (current.back() == '\n' || current.back() == '\r')) current.pop_back();
    UP::Version v;
    if (!UP::ParseVersion(current, v) || UP::IsZero(v)) return L"";
    // Never hand over to a version whose install just failed (rollback keeps/relaunches the previous app).
    std::string result;
    if (UP::ReadSmallFile(root + L"\\updates\\last-result", 4096, result)) {
        auto lines = Lines(result);
        if (!lines.empty() && lines[0] == "browser-result 1" && Field(lines, "status") == "failed" && Field(lines, "version") == current)
            return L"";
    }
    const std::wstring dir = versions + L"\\" + UP::Widen(current), target = dir + L"\\browser.exe";
    if (!UP::IsDirectory(dir) || UP::IsReparsePoint(dir)) return L"";
    DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return L"";
    if (Lower(target) == Lower(exe)) return L"";
    return target;
}

// ---------------------------------------------------------------- URL policy

namespace {
struct UrlParts {
    std::wstring scheme, host, path, user, password;
    INTERNET_PORT port = 0;
};
bool Crack(const std::wstring& url, UrlParts& out) {
    if (url.size() > 4096) return false;
    for (wchar_t c : url)
        if (c <= 0x20 || c >= 0x7f) return false;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = DWORD(-1);
    uc.dwUserNameLength = uc.dwPasswordLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &uc)) return false;
    out.scheme.assign(uc.lpszScheme, uc.dwSchemeLength);
    out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (uc.lpszUserName) out.user.assign(uc.lpszUserName, uc.dwUserNameLength);
    if (uc.lpszPassword) out.password.assign(uc.lpszPassword, uc.dwPasswordLength);
    out.port = uc.nPort;
    // Reject explicit default-port tricks such as "github.com:443@evil" by requiring the authority to be exactly host[:443].
    auto authorityStart = url.find(L"://");
    if (authorityStart == std::wstring::npos) return false;
    auto authorityEnd = url.find(L'/', authorityStart + 3);
    std::wstring authority = Lower(url.substr(authorityStart + 3, authorityEnd == std::wstring::npos ? std::wstring::npos
                                                                                                       : authorityEnd - authorityStart - 3));
    std::wstring host = Lower(out.host);
    return authority == host || authority == host + L":443";
}
}  // namespace

bool AllowedUrl(const std::wstring& url, const std::wstring& repository, bool initial) {
    UrlParts u;
    if (!UP::ValidRepository(repository) || !Crack(url, u)) return false;
    if (Lower(u.scheme) != L"https" || u.port != 443 || !u.user.empty() || !u.password.empty()) return false;
    std::wstring host = Lower(u.host);
    if (initial)
        return host == L"github.com" && Lower(u.path).rfind(Lower(L"/" + repository + L"/releases/"), 0) == 0 &&
               u.path.find(L"..") == std::wstring::npos;
    return host == L"github.com" || host == L"objects.githubusercontent.com" || host == L"release-assets.githubusercontent.com";
}
std::wstring ManifestUrl(const std::wstring& repository) {
    return L"https://github.com/" + repository + L"/releases/latest/download/" + W(UP::ManifestAssetName);
}
std::wstring PackageUrl(const std::wstring& repository, const std::string& version) {
    return L"https://github.com/" + repository + L"/releases/download/v" + W(version) + L"/" + W(UP::PackageFileName(version));
}

// ---------------------------------------------------------------- WinHTTP transport

namespace {
class WinHttpTransport : public Transport {
public:
    WinHttpTransport() {
        session_ = WinHttpOpen(L"browser-updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_)
            session_ = WinHttpOpen(L"browser-updater/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_) {
            WinHttpSetTimeouts(session_, 10000, 10000, 15000, 20000);
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
                protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
            }
        }
    }
    ~WinHttpTransport() override {
        if (session_) WinHttpCloseHandle(session_);
    }
    void Abort() override {
        std::lock_guard<std::mutex> lock(mutex_);
        aborted_ = true;
        if (request_) {
            WinHttpCloseHandle(request_);
            request_ = nullptr;
        }
    }
    HttpResult Get(const std::wstring& startUrl, std::uint64_t maxBytes, const std::function<bool(std::uint64_t)>& onLength,
                   const std::function<bool(const void*, std::size_t)>& sink, const std::atomic<bool>& cancel) override {
        HttpResult r;
        if (!session_) return r.error = "winhttp unavailable", r;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            aborted_ = false;
        }
        std::wstring url = startUrl;
        for (int hop = 0; hop < 6; ++hop) {
            if (cancel.load()) return r.status = HttpResult::Cancelled, r;
            UrlParts u;
            if (!Crack(url, u) || Lower(u.scheme) != L"https" || u.port != 443) return r.status = HttpResult::Rejected, r.error = "url rejected", r;
            HINTERNET connect = WinHttpConnect(session_, u.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!connect) return r.error = "connect failed", r;
            HINTERNET request = WinHttpOpenRequest(connect, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (!request) {
                WinHttpCloseHandle(connect);
                return r.error = "request failed", r;
            }
            DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
            if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable))) {
                // Fail closed (as src/webview2-install.h does): never fetch with redirects, cookies or auth on.
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                return r.error = "request options failed", r;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (aborted_ || cancel.load()) {
                    WinHttpCloseHandle(request);
                    WinHttpCloseHandle(connect);
                    return r.status = HttpResult::Cancelled, r;
                }
                request_ = request;
            }
            auto finish = [&](HttpResult res) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (request_) WinHttpCloseHandle(request_);
                    request_ = nullptr;
                    if (aborted_ || cancel.load()) res.status = HttpResult::Cancelled;
                }
                WinHttpCloseHandle(connect);
                return res;
            };
            if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(request, nullptr)) {
                r.error = "network error " + std::to_string(GetLastError());
                return finish(r);
            }
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);
            r.httpStatus = status;
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                wchar_t location[4096];
                DWORD bytes = sizeof(location);
                if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location, &bytes,
                                         WINHTTP_NO_HEADER_INDEX)) {
                    r.error = "redirect without location";
                    return finish(r);
                }
                std::wstring next(location, bytes / sizeof(wchar_t));
                if (!redirectAllowed(next)) {
                    r.status = HttpResult::Rejected;
                    r.error = "redirect rejected";
                    return finish(r);
                }
                finish(r);
                url = next;
                continue;
            }
            if (status == 404) {
                r.status = HttpResult::NotFound;
                return finish(r);
            }
            if (status != 200) {
                r.error = "http " + std::to_string(status);
                return finish(r);
            }
            std::uint64_t length = 0;
            wchar_t lengthText[32];
            DWORD lengthBytes = sizeof(lengthText);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText,
                                    &lengthBytes, WINHTTP_NO_HEADER_INDEX))
                length = std::wcstoull(lengthText, nullptr, 10);
            if (length > maxBytes) {
                r.status = HttpResult::TooLarge;
                return finish(r);
            }
            if (onLength && !onLength(length)) {
                r.status = HttpResult::Rejected;
                r.error = "length rejected";
                return finish(r);
            }
            std::vector<char> buffer(64 * 1024);
            std::uint64_t total = 0;
            for (;;) {
                if (cancel.load()) {
                    r.status = HttpResult::Cancelled;
                    return finish(r);
                }
                DWORD got = 0;
                if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &got)) {
                    r.error = "read error " + std::to_string(GetLastError());
                    return finish(r);
                }
                if (!got) break;
                total += got;
                if (total > maxBytes) {
                    r.status = HttpResult::TooLarge;
                    return finish(r);
                }
                if (!sink(buffer.data(), got)) {
                    r.status = HttpResult::Rejected;
                    r.error = "sink rejected";
                    return finish(r);
                }
            }
            r.status = HttpResult::Ok;
            return finish(r);
        }
        r.error = "too many redirects";
        return r;
    }
    std::function<bool(const std::wstring&)> redirectAllowed;

private:
    HINTERNET session_ = nullptr;
    std::mutex mutex_;
    HINTERNET request_ = nullptr;
    bool aborted_ = false;
};
}  // namespace

std::shared_ptr<Transport> CreateWinHttpTransport() {
    auto t = std::make_shared<WinHttpTransport>();
    t->redirectAllowed = [](const std::wstring& url) { return AllowedUrl(url, L"x/y", false); };
    return t;
}

// ---------------------------------------------------------------- startup acknowledgment

BrowserUpdate::View NoteStartup(const Config& config) {
    BrowserUpdate::View view;
    UP::Version current;
    if (!UP::ParseVersion(config.currentVersion, current)) return view;
    std::wstring root = FullPath(config.installRoot.empty() ? DefaultInstallRoot() : config.installRoot);
    if (root.empty()) return view;
    std::wstring updates = root + L"\\updates";
    std::string text;
    std::string version = UP::Narrow(config.currentVersion);
    if (UP::ReadSmallFile(updates + L"\\pending", 4096, text)) {
        auto lines = Lines(text);
        std::string nonce = Field(lines, "nonce");
        bool nonceOk = nonce.size() == 32 && nonce.find_first_not_of("0123456789abcdef") == std::string::npos;
        std::wstring expectedExe = Lower(root + L"\\versions\\" + config.currentVersion + L"\\browser.exe");
        if (!lines.empty() && lines[0] == "browser-pending 1" && nonceOk && Field(lines, "version") == version &&
            Lower(FullPath(config.exePath)) == expectedExe) {
            HANDLE ack = CreateFileW((updates + L"\\ack-" + W(nonce)).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
            if (ack != INVALID_HANDLE_VALUE) {
                std::string body = "browser-ack 1\nversion " + version + "\npid " + std::to_string(GetCurrentProcessId()) + "\n";
                DWORD put = 0;
                bool ok = WriteFile(ack, body.data(), DWORD(body.size()), &put, nullptr) && put == body.size();
                FlushFileBuffers(ack);
                CloseHandle(ack);
                if (ok) {
                    view.phase = Phase::Installed;
                    view.version = config.currentVersion;
                    return view;
                }
            }
        }
    }
    if (UP::ReadSmallFile(updates + L"\\last-result", 8192, text)) {
        auto lines = Lines(text);
        if (!lines.empty() && lines[0] == "browser-result 1" && Field(lines, "status") == "failed" &&
            Field(lines, "reported") == "0") {
            std::string failed = Field(lines, "version");
            UP::Version fv;
            std::string rewritten;
            for (auto& l : lines) rewritten += (l == "reported 0" ? std::string("reported 1") : l) + "\n";
            if (UP::AtomicWriteFile(updates + L"\\last-result", rewritten) && UP::ParseVersion(failed, fv)) {
                view.phase = Phase::Error;
                view.version = W(failed);
                view.error = TRF(update_error_install_failed_kept, W(failed));
            }
        }
    }
    return view;
}

// ---------------------------------------------------------------- engine

Engine::~Engine() { Shutdown(); }

std::int64_t Engine::Now() const {
    if (config_.clock) return config_.clock();
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return std::int64_t(u.QuadPart / 10000000ull) - 11644473600ll;
}
std::uint64_t Engine::Ticks() const { return config_.ticks ? config_.ticks() : GetTickCount64(); }

bool Engine::EnsureDir(const std::wstring& dir) const {
    if (UP::IsDirectory(dir)) return !UP::IsReparsePoint(dir);
    std::wstring parent = Parent(dir);
    if (!parent.empty() && parent != dir && !UP::IsDirectory(parent) && !EnsureDir(parent)) return false;
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

BrowserUpdate::View Engine::Make(Phase phase, const std::string& version, const std::wstring& error) const {
    BrowserUpdate::View v;
    v.phase = phase;
    v.version = W(version);
    v.error = error;
    return v;
}

Engine::State Engine::LoadState() const {
    State s;
    std::string text;
    if (!UP::ReadSmallFile(Updates() + L"\\state", 4096, text)) return s;
    auto lines = Lines(text);
    if (lines.empty() || lines[0] != "browser-update-state 1") return s;
    s.nextCheck = std::atoll(Field(lines, "next-check").c_str());
    s.failures = std::uint32_t(std::min(30ll, std::max(0ll, std::atoll(Field(lines, "failures").c_str()))));
    s.notified = Field(lines, "notified");
    if (s.notified.size() > 64) s.notified.clear();
    return s;
}
void Engine::SaveState(const State& s) const {
    if (!EnsureDir(Updates())) return;
    UP::AtomicWriteFile(Updates() + L"\\state", "browser-update-state 1\nnext-check " + std::to_string(s.nextCheck) +
                                                    "\nfailures " + std::to_string(s.failures) + "\nnotified " +
                                                    (s.notified.empty() ? "-" : s.notified) + "\n");
}

bool Engine::Start(const Config& config) {
    if (started_) return false;
    started_ = true;
    config_ = config;
    root_ = FullPath(config.installRoot.empty() ? DefaultInstallRoot() : config.installRoot);
    repository_ = config.repository.empty() ? DefaultRepository() : config.repository;
    current_ = UP::Narrow(config.currentVersion);
    session_ = WPARAM(std::strtoull(RandomHex(4).c_str(), nullptr, 16) | 1);
    enabled_ = config.enabled && !root_.empty() && UP::ParseVersion(current_, currentVersion_) && !UP::IsZero(currentVersion_) &&
               UP::ValidRepository(repository_);
    BrowserUpdate::View startup = NoteStartup(config_);
    if (startup.phase != Phase::Hidden) {
        std::lock_guard<std::mutex> lock(mutex_);
        view_ = startup;
        if (startup.phase == Phase::Error) { failedOp_ = Op::Check; failure_ = Failure::Install; }  // last-result: install failed
    }
    if (startup.phase != Phase::Hidden) Post();
    if (!enabled_) return true;
    transport_ = config.transport ? config.transport : CreateWinHttpTransport();
    if (config.automaticCheck) Launch(Op::Check, false, true);
    return true;
}

void Engine::Post() {
    if (!config_.notifyWindow || !config_.notifyMessage) return;
    if (notifyPending_.exchange(true)) return;
    if (!PostMessageW(config_.notifyWindow, config_.notifyMessage, session_, 0)) notifyPending_ = false;
}

bool Engine::OnNotify(WPARAM generation) {
    if (generation != session_) return false;
    notifyPending_ = false;
    return true;
}

BrowserUpdate::View Engine::View() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return view_;
}
Engine::LastCheck Engine::LastCheckResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastCheck_;
}
Engine::Failure Engine::LastFailure() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failure_;
}

bool Engine::Publish(std::uint64_t gen, const BrowserUpdate::View& view) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gen != generation_ || shutdown_) return false;
        view_ = view;
    }
    Post();
    return true;
}

void Engine::SetView(const BrowserUpdate::View& v) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        view_ = v;
    }
    Post();
}

bool Engine::Wait(DWORD ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return !wake_.wait_for(lock, std::chrono::milliseconds(ms), [&] { return cancel_.load() || shutdown_.load(); });
}

void Engine::StopWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;  // anything the old worker publishes from now on is stale
        cancel_ = true;
    }
    wake_.notify_all();
    if (transport_) transport_->Abort();
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
}

void Engine::Launch(Op op, bool manual, bool startup) {
    if (shutdown_) return;
    StopWorker();
    std::uint64_t gen;
    UP::Manifest manifest;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        gen = ++generation_;
        manifest = manifest_;
        // A manual check reads as Checking at once (menu, drawer, Retry), not after the worker's first publish;
        // set under the generation bump so no stale worker can overwrite it.
        if (op == Op::Check && manual) view_ = Make(Phase::Checking, "");
    }
    if (op == Op::Check && manual) Post();
    busy_ = true;
    if (op == Op::Check)
        worker_ = std::thread([this, gen, manual, startup] { RunCheck(gen, manual, startup); Finished(); });
    else
        worker_ = std::thread([this, gen, manifest] { RunDownload(gen, manifest); Finished(); });
}

// Worker done (also quiet automatic checks that publish no view): one coalesced notification lets the host
// re-arm its single due-aware timer from the freshly persisted next-check.
void Engine::Finished() {
    busy_ = false;
    if (!shutdown_ && !cancel_) Post();
}

bool Engine::AutoCheck() {
    if (!enabled_ || shutdown_ || !config_.automaticCheck || busy_) return false;
    if (View().phase != Phase::Hidden) return false;  // never interrupt an offer, a download, an install or an error
    State state = LoadState();
    const std::int64_t now = Now();
    if (now < state.nextCheck && state.nextCheck - now <= FailureMax) return false;  // TTL / back-off: no request
    Launch(Op::Check, false, false);
    return true;
}

DWORD Engine::AutoCheckDelayMs() const {
    if (!enabled_ || shutdown_ || !config_.automaticCheck) return 0;
    State state = LoadState();
    const std::int64_t wait = state.nextCheck - Now();
    if (wait <= 0 || wait > FailureMax) return AutoCheckMinMs;  // due (or corrupt): retry no sooner than the floor
    const std::int64_t ms = wait * 1000;
    return DWORD(std::min<std::int64_t>(AutoCheckMaxMs, std::max<std::int64_t>(AutoCheckMinMs, ms)));
}

void Engine::Shutdown() {
    if (shutdown_.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        cancel_ = true;
    }
    wake_.notify_all();
    if (transport_) transport_->Abort();
    if (worker_.joinable()) worker_.join();
}

bool Engine::FindStaged(UP::Manifest*, std::string& version) {
    std::wstring staging = Updates() + L"\\staging";
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW((staging + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return false;
    UP::Version best = currentVersion_;
    std::string bestName;
    do {
        UP::Version v;
        std::string name = UP::Narrow(fd.cFileName);
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            UP::ParseVersion(name, v) && UP::CompareVersion(v, best) > 0) {
            best = v;
            bestName = name;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    if (bestName.empty()) return false;
    std::string error;
    if (!UP::VerifyStagedTree(staging + L"\\" + W(bestName), bestName, error)) return false;
    version = bestName;
    return true;
}

void Engine::RunCheck(std::uint64_t gen, bool manual, bool startup) {
    if (startup && config_.startupDelayMs && !Wait(config_.startupDelayMs)) return;
    if (cancel_) return;
    State state = LoadState();
    std::string staged;
    bool haveStaged = FindStaged(nullptr, staged);
    if (haveStaged) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (gen != generation_) return;
            stagedVersion_ = staged;
        }
        if (manual || state.notified != "ready:" + staged) {
            if (Publish(gen, Make(Phase::Ready, staged)) && !manual) {
                state.notified = "ready:" + staged;
                SaveState(state);
            }
        }
        return;
    }
    if (!manual && Now() < state.nextCheck && state.nextCheck - Now() <= FailureMax) return;  // TTL / backoff, quiet
    if (manual) Publish(gen, Make(Phase::Checking, ""));
    std::string body;
    HttpResult r = transport_->Get(
        ManifestUrl(repository_), UP::MaxManifestBytes, nullptr,
        [&](const void* p, std::size_t n) {
            body.append(static_cast<const char*>(p), n);
            return body.size() <= UP::MaxManifestBytes;
        },
        cancel_);
    if (r.status == HttpResult::Cancelled || cancel_) return;
    UP::Manifest m;
    std::string error;
    bool failed = false, upToDate = false;
    if (r.status == HttpResult::NotFound) {
        upToDate = true;  // no published release yet
    } else if (r.status != HttpResult::Ok || !UP::ParseManifest(body, m, error)) {
        failed = true;
    } else {
        UP::Version v;
        UP::ParseVersion(m.version, v);
        upToDate = UP::CompareVersion(v, currentVersion_) <= 0;  // never downgrade, never reinstall
    }
    if (failed) {
        state.failures = std::min<std::uint32_t>(state.failures + 1, 30);
        std::int64_t backoff = FailureBase << std::min<std::uint32_t>(state.failures - 1, 5);
        state.nextCheck = Now() + std::min(backoff, FailureMax);
        SaveState(state);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (gen != generation_) return;
            lastCheck_ = LastCheck::Failed;
            failedOp_ = Op::Check;
            failure_ = Failure::Check;
        }
        if (manual) Publish(gen, Make(Phase::Error, "", TR(update_error_check_failed)));
        return;
    }
    state.failures = 0;
    state.nextCheck = Now() + SuccessTtl;
    if (upToDate) {
        SaveState(state);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (gen != generation_) return;
            lastCheck_ = LastCheck::UpToDate;
        }
        if (manual) Publish(gen, Make(Phase::Hidden, ""));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gen != generation_) return;
        manifest_ = m;
        haveManifest_ = true;
        lastCheck_ = LastCheck::Available;
    }
    BrowserUpdate::View view = Make(Phase::Available, m.version);
    view.total = m.size;
    if (manual || state.notified != "available:" + m.version) {
        if (Publish(gen, view) && !manual) state.notified = "available:" + m.version;
    }
    SaveState(state);
}

void Engine::RunDownload(std::uint64_t gen, UP::Manifest m) {
    auto fail = [&](const std::wstring& message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (gen != generation_) return;
            failedOp_ = Op::Download;
            failure_ = Failure::Download;
        }
        BrowserUpdate::View v = Make(Phase::Error, m.version, message);
        v.total = m.size;
        Publish(gen, v);
    };
    std::wstring downloads = Updates() + L"\\downloads", staging = Updates() + L"\\staging";
    if (!EnsureDir(downloads) || !EnsureDir(staging)) return fail(TR(update_error_prepare_folder));
    std::wstring partial = downloads + L"\\" + W(m.package) + L".partial";
    UP::RemoveTree(partial);
    HANDLE file = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return fail(TR(update_error_write));
    UP::Sha256 hash;
    std::uint64_t received = 0, lastPublish = 0, start = Ticks(), rateAt = start, rateBytes = 0;
    double rate = 0;
    bool writeFailed = false;
    auto progress = [&](bool force) {
        std::uint64_t now = Ticks();
        if (!force && now - lastPublish < ProgressIntervalMs) return;
        lastPublish = now;
        if (now - rateAt >= 250) {
            double inst = double(received - rateBytes) * 1000.0 / double(now - rateAt);
            rate = rate <= 0 ? inst : rate * 0.7 + inst * 0.3;
            rateAt = now;
            rateBytes = received;
        }
        BrowserUpdate::View v = Make(Phase::Downloading, m.version);
        v.received = received;
        v.total = m.size;
        if (now - start >= 1000 && received >= 64 * 1024 && rate > 1) v.secondsRemaining = double(m.size - received) / rate;
        Publish(gen, v);
    };
    progress(true);
    HttpResult r = transport_->Get(
        PackageUrl(repository_, m.version), m.size, [&](std::uint64_t length) { return length == 0 || length == m.size; },
        [&](const void* p, std::size_t n) {
            DWORD put = 0;
            if (!WriteFile(file, p, DWORD(n), &put, nullptr) || put != n) {
                writeFailed = true;
                return false;
            }
            hash.Update(p, n);
            received += n;
            progress(false);
            return true;
        },
        cancel_);
    bool flushed = FlushFileBuffers(file);
    CloseHandle(file);
    auto discard = [&] { DeleteFileW(partial.c_str()); };
    if (r.status == HttpResult::Cancelled || cancel_) return discard();
    if (writeFailed || !flushed) return discard(), fail(TR(update_error_disk));
    if (r.status != HttpResult::Ok) return discard(), fail(TR(update_error_download_failed));
    if (received != m.size || hash.Final() != m.sha256)
        return discard(), fail(TR(update_error_package_digest));
    progress(true);
    std::wstring target = staging + L"\\" + W(m.version);
    std::wstring work = target + L".partial-" + W(RandomHex(8));
    std::string error;
    bool extracted = UP::ExtractPackage(partial, m.version, work, &cancel_, error);
    discard();
    if (cancel_) return void(UP::RemoveTree(work));
    if (!extracted) return fail(TR(update_error_package_invalid));
    if (UP::PathExists(target) && !UP::RemoveTree(target)) {
        UP::RemoveTree(work);
        return fail(TR(update_error_locked));
    }
    // A scanner/indexer may still hold a file just extracted into `work`: the rename is retried briefly.
    if (!UP::MoveDirectory(work, target, 15 * 1000)) {
        UP::RemoveTree(work);
        return fail(TR(update_error_prepare));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gen != generation_) return;
        stagedVersion_ = m.version;
    }
    BrowserUpdate::View v = Make(Phase::Ready, m.version);
    v.received = v.total = m.size;
    Publish(gen, v);
}

void Engine::CheckNow() {
    if (!enabled_ || shutdown_) return;
    Phase phase = View().phase;
    if (phase == Phase::Downloading || phase == Phase::Installing || phase == Phase::Checking) return;
    std::string staged;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        staged = stagedVersion_;
    }
    if (!staged.empty()) {
        StopWorker();
        SetView(Make(Phase::Ready, staged));
        return;
    }
    Launch(Op::Check, true, false);
}

ActionResult Engine::Perform(Action action) {
    if (shutdown_) return ActionResult::Ignored;
    BrowserUpdate::View view = View();
    switch (action) {
    case Action::Download: {
        if (!enabled_ || view.phase != Phase::Available) return ActionResult::Ignored;
        bool have;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            have = haveManifest_;
        }
        if (!have) {
            Launch(Op::Check, true, false);
            return ActionResult::Handled;
        }
        Launch(Op::Download, true, false);
        return ActionResult::Handled;
    }
    case Action::Cancel: {
        if (view.phase != Phase::Checking && view.phase != Phase::Downloading) return ActionResult::Ignored;
        StopWorker();
        if (view.phase == Phase::Downloading) {
            BrowserUpdate::View v = Make(Phase::Available, UP::Narrow(view.version));
            v.total = view.total;
            SetView(v);
        } else {
            SetView(BrowserUpdate::View{});
        }
        return ActionResult::Handled;
    }
    case Action::Retry: {
        if (view.phase != Phase::Error || !enabled_) return ActionResult::Ignored;
        Op op;
        bool have;
        std::string available;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            op = failedOp_;
            have = haveManifest_;
            available = manifest_.version;
        }
        if (op == Op::Download && have) {
            SetView(Make(Phase::Available, available));  // transient; download publishes immediately
            Launch(Op::Download, true, false);
        } else {
            Launch(Op::Check, true, false);
        }
        return ActionResult::Handled;
    }
    case Action::Dismiss: {
        if (view.phase == Phase::Hidden || view.phase == Phase::Downloading || view.phase == Phase::Installing ||
            view.phase == Phase::Checking)
            return ActionResult::Ignored;
        if (view.phase == Phase::Available || view.phase == Phase::Ready) {
            State s = LoadState();
            s.notified = std::string(view.phase == Phase::Ready ? "ready:" : "available:") + UP::Narrow(view.version);
            SaveState(s);
        }
        SetView(BrowserUpdate::View{});
        return ActionResult::Handled;
    }
    case Action::RestartInstall:
        break;
    }
    // RestartInstall
    if (!enabled_ || view.phase != Phase::Ready) return ActionResult::Ignored;
    std::string version;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        version = stagedVersion_;
    }
    auto failInstall = [&](const std::wstring& message) {
        SetView(Make(Phase::Error, version, message));
        std::lock_guard<std::mutex> lock(mutex_);
        failedOp_ = Op::Check;
        failure_ = Failure::Install;
        stagedVersion_.clear();
        return ActionResult::Handled;
    };
    StopWorker();
    std::wstring stage = Updates() + L"\\staging\\" + W(version);
    std::string error;
    if (version.empty() || !UP::VerifyStagedTree(stage, version, error))
        return failInstall(TR(update_error_prepared_invalid));
    std::string nonce = RandomHex(16);
    std::wstring helperDir = Updates() + L"\\helper";
    std::wstring helper = helperDir + L"\\update-helper-" + W(nonce) + L".exe";
    {   // Copies left by earlier installs (SEC-16): a finished helper cannot delete its own file. One still running is locked and stays.
        WIN32_FIND_DATAW fd;
        HANDLE f = UP::IsReparsePoint(helperDir) ? INVALID_HANDLE_VALUE : FindFirstFileW((helperDir + L"\\update-helper-*.exe").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) DeleteFileW((helperDir + L"\\" + fd.cFileName).c_str());
            while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    if (!EnsureDir(helperDir) || !CopyFileW((stage + L"\\update-helper.exe").c_str(), helper.c_str(), TRUE))
        return failInstall(TR(update_error_launch_install));
    std::wstring exe = FullPath(config_.exePath);
    DataPaths paths = ResolveDataPaths(exe, root_);
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    std::uint64_t createdValue = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    std::string plan = "browser-install 1\nnonce " + nonce + "\nversion " + version + "\npid " +
                       std::to_string(GetCurrentProcessId()) + "\ncreated " + std::to_string(createdValue) +
                       "\nprevious-kind " + (paths.installed ? "installed" : "portable") + "\nprevious-version " + current_ +
                       "\nprevious-exe " + UP::Narrow(exe) + "\n";
    std::wstring planPath = Updates() + L"\\install-" + W(nonce);
    if (!UP::AtomicWriteFile(planPath, plan)) {
        DeleteFileW(helper.c_str());
        return failInstall(TR(update_error_launch_install));
    }
    std::wstring commandLine = L"\"" + helper + L"\" --install " + W(nonce);  // helper path is under root, nonce is hex
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(helper.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                             CREATE_BREAKAWAY_FROM_JOB | CREATE_NO_WINDOW, nullptr, helperDir.c_str(), &si, &pi);
    if (!ok)
        ok = CreateProcessW(helper.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            helperDir.c_str(), &si, &pi);
    if (!ok) {
        DeleteFileW(planPath.c_str());
        DeleteFileW(helper.c_str());
        return failInstall(TR(update_error_launch_install));
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    SetView(Make(Phase::Installing, version));
    return ActionResult::CloseForInstall;
}

}  // namespace UpdateCore
