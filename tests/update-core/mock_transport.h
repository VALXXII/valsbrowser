#pragma once
// Header-only test transports for UpdateCore (no network). Production HTTPS policy is not weakened:
// these only replace the Transport interface the engine is given in tests.
#include <windows.h>

#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

#include "../../src/update-core.h"

namespace UpdateTest {

struct MockResponse {
    UpdateCore::HttpResult::Status status = UpdateCore::HttpResult::Ok;
    std::string body;
    std::uint64_t declaredLength = UINT64_MAX;  // UINT64_MAX => body size
    std::size_t chunk = 64 * 1024;
    DWORD chunkDelayMs = 0;
};

// In-memory transport keyed by exact URL. Unknown URL => Failed (offline).
class MockTransport : public UpdateCore::Transport {
public:
    void Set(const std::wstring& url, MockResponse r) {
        std::lock_guard<std::mutex> lock(mutex_);
        responses_[url] = std::move(r);
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        responses_.clear();
    }
    int Requests() const { return requests_.load(); }
    void Abort() override { aborted_ = true; }
    UpdateCore::HttpResult Get(const std::wstring& url, std::uint64_t maxBytes, const std::function<bool(std::uint64_t)>& onLength,
                               const std::function<bool(const void*, std::size_t)>& sink, const std::atomic<bool>& cancel) override {
        ++requests_;
        aborted_ = false;
        UpdateCore::HttpResult r;
        MockResponse resp;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = responses_.find(url);
            if (it == responses_.end()) return r.error = "offline", r;
            resp = it->second;
        }
        if (resp.status != UpdateCore::HttpResult::Ok) return r.status = resp.status, r;
        std::uint64_t length = resp.declaredLength == UINT64_MAX ? resp.body.size() : resp.declaredLength;
        if (length > maxBytes) return r.status = UpdateCore::HttpResult::TooLarge, r;
        if (onLength && !onLength(length)) return r.status = UpdateCore::HttpResult::Rejected, r;
        std::uint64_t sent = 0;
        while (sent < resp.body.size()) {
            if (cancel.load() || aborted_.load()) return r.status = UpdateCore::HttpResult::Cancelled, r;
            if (resp.chunkDelayMs) Sleep(resp.chunkDelayMs);
            if (cancel.load() || aborted_.load()) return r.status = UpdateCore::HttpResult::Cancelled, r;
            std::size_t n = std::size_t(std::min<std::uint64_t>(resp.chunk, resp.body.size() - sent));
            if (sent + n > maxBytes) return r.status = UpdateCore::HttpResult::TooLarge, r;
            if (!sink(resp.body.data() + sent, n)) return r.status = UpdateCore::HttpResult::Rejected, r;
            sent += n;
        }
        r.status = UpdateCore::HttpResult::Ok;
        r.httpStatus = 200;
        return r;
    }

private:
    std::mutex mutex_;
    std::map<std::wstring, MockResponse> responses_;
    std::atomic<int> requests_{0};
    std::atomic<bool> aborted_{false};
};

// Serves "<feedDir>\<last URL segment>" for URLs accepted by the production initial-URL policy.
class DiskTransport : public UpdateCore::Transport {
public:
    DiskTransport(std::wstring feedDir, std::wstring repository) : dir_(std::move(feedDir)), repo_(std::move(repository)) {}
    UpdateCore::HttpResult Get(const std::wstring& url, std::uint64_t maxBytes, const std::function<bool(std::uint64_t)>& onLength,
                               const std::function<bool(const void*, std::size_t)>& sink, const std::atomic<bool>& cancel) override {
        UpdateCore::HttpResult r;
        if (!UpdateCore::AllowedUrl(url, repo_, true)) return r.status = UpdateCore::HttpResult::Rejected, r;
        std::wstring name = url.substr(url.find_last_of(L'/') + 1);
        std::string body;
        if (!UpdatePackage::ReadSmallFile(dir_ + L"\\" + name, UpdatePackage::MaxPackageBytes, body))
            return r.status = UpdateCore::HttpResult::NotFound, r;
        if (body.size() > maxBytes) return r.status = UpdateCore::HttpResult::TooLarge, r;
        if (onLength && !onLength(body.size())) return r.status = UpdateCore::HttpResult::Rejected, r;
        for (std::size_t i = 0; i < body.size(); i += 32768) {
            if (cancel.load()) return r.status = UpdateCore::HttpResult::Cancelled, r;
            if (!sink(body.data() + i, std::min<std::size_t>(32768, body.size() - i))) return r.status = UpdateCore::HttpResult::Rejected, r;
        }
        r.status = UpdateCore::HttpResult::Ok;
        return r;
    }

private:
    std::wstring dir_, repo_;
};

}  // namespace UpdateTest
