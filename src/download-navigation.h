#pragma once
// A top-level navigation whose response becomes a download (Content-Disposition: attachment, a type the
// engine does not render, a short link or redirect to such a file) is cancelled by the engine:
// NavigationCompleted reports a failure (CONNECTION_ABORTED, measured on the real engine, or OPERATION_CANCELED) and
// DownloadStarting fires for the same URL, in either order. Like Chrome and Edge, that is a download, never a
// network error: the tab keeps its page and its address.
//
// One Tracker per tab pairs the two events:
//  - by navigation: the URLs of the tab's latest top-level navigation (its id and every redirect hop seen by
//    NavigationStarting); the download's URL must be one of them;
//  - by time: the two events at most WaitMs apart, and the download not older than the navigation.
// A second automatic download from a site first raises the engine's « download multiple files? » question
// (MULTIPLE_AUTOMATIC_DOWNLOADS, for the site's origin) instead of DownloadStarting: DownloadAsked() pairs it the
// same way, by the origin of one of the navigation's URLs.
// A failure whose download has not arrived yet is held (Wait) until DownloadStarted() settles it or the
// caller's timer calls Expire() (then the error page is shown as before). Failures a download never produces
// (DNS, refused, reset, timeout, offline, an HTTP 4xx/5xx page...) are never held. A download whose URL is not
// the navigation's (a page-made blob, another link) never swallows a genuine error.
// Pure (no Windows, no engine): tests/download_navigation_test.cpp.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace NativeChrome::DownloadNavigation {

// Longest gap accepted between NavigationCompleted and DownloadStarting (both orders), and how long a
// possible download's failure is held before its error page shows.
constexpr std::uint64_t WaitMs = 1500;

// COREWEBVIEW2_WEB_ERROR_STATUS values the engine reports for a navigation it turned into a download. Never UNKNOWN:
// a response cut after its 200 headers or a refused connection report UNKNOWN and keep their error page at once.
constexpr int StatusConnectionAborted = 9, StatusOperationCanceled = 14;
inline bool DownloadLike(int webErrorStatus, int /*httpStatus*/) {
    return webErrorStatus == StatusConnectionAborted || webErrorStatus == StatusOperationCanceled;
}
// The fragment never reaches the server: ignored when comparing.
inline std::wstring Key(const std::wstring& uri) { const auto hash = uri.find(L'#'); return hash == std::wstring::npos ? uri : uri.substr(0, hash); }
// scheme://host[:port] (the engine gives canonical, lower-case URLs); empty when there is none.
inline std::wstring Origin(const std::wstring& uri) {
    const auto scheme = uri.find(L"://"); if (scheme == std::wstring::npos) return L"";
    const auto end = uri.find_first_of(L"/?#", scheme + 3); return uri.substr(0, end == std::wstring::npos ? uri.size() : end);
}

enum class Verdict { ErrorPage, Download, Wait };

struct Tracker {
    std::uint64_t id = 0, startedAt = 0;   // the tab's latest top-level navigation
    std::vector<std::wstring> chain;        // its URLs (first one, then each redirect hop)
    bool held = false, download = false;    // its failure waits for a download / it became a download
    bool asked = false;                     // settled by the « download multiple files » question, not by a download yet
    std::uint64_t failedAt = 0;
    struct Seen { std::wstring uri; std::uint64_t at; bool origin = false; };   // origin: a question for that site
    std::vector<Seen> downloads;            // downloads of this tab not paired yet (recent ones only)
    std::vector<std::wstring> questions;    // origins whose « download multiple files? » was raised since the tab's last
                                            // shown page: while it is open, or once refused, the engine blocks that site's
                                            // further downloads without any event: those are not errors either

    bool InChain(const std::wstring& key) const { return std::find(chain.begin(), chain.end(), key) != chain.end(); }
    bool OriginInChain(const std::wstring& origin) const {
        return !origin.empty() && std::any_of(chain.begin(), chain.end(), [&](const std::wstring& u) { return Origin(u) == origin; });
    }
    bool Matches(const Seen& s) const { return s.origin ? OriginInChain(s.uri) : InChain(s.uri); }
    void Prune(std::uint64_t now) {
        downloads.erase(std::remove_if(downloads.begin(), downloads.end(), [&](const Seen& s) { return now > s.at + WaitMs; }), downloads.end());
        if (downloads.size() > 16) downloads.erase(downloads.begin(), downloads.end() - 16);
    }
    // NavigationStarting (top level): a new id starts a new navigation, the same id is a redirect hop.
    void Starting(std::uint64_t navigationId, const std::wstring& uri, std::uint64_t now) {
        if (navigationId != id || chain.empty()) { id = navigationId; startedAt = now; chain.clear(); held = download = asked = false; failedAt = 0; }
        const auto key = Key(uri);
        if (!InChain(key)) chain.push_back(key);
        Prune(now);
    }
    // NavigationCompleted with IsSuccess=false.
    Verdict Failed(std::uint64_t navigationId, int webErrorStatus, int httpStatus, std::uint64_t now) {
        if (navigationId != id || chain.empty()) return Verdict::ErrorPage;   // not the latest navigation: existing path
        if (download) return Verdict::Download;
        if (!DownloadLike(webErrorStatus, httpStatus)) return Verdict::ErrorPage;
        Prune(now);
        for (auto it = downloads.begin(); it != downloads.end(); ++it)
            if (it->at >= startedAt && now <= it->at + WaitMs && Matches(*it)) { asked = it->origin; downloads.erase(it); download = true; held = false; return Verdict::Download; }
        for (const auto& origin : questions)
            if (OriginInChain(origin)) { download = asked = true; held = false; return Verdict::Download; }
        held = true; failedAt = now; return Verdict::Wait;
    }
    // DownloadStarting in this tab. True when it settles the held failure: that navigation was a download.
    bool DownloadStarted(const std::wstring& uri, std::uint64_t now) {
        const auto key = Key(uri);
        if (held && !download && now <= failedAt + WaitMs && InChain(key)) { held = false; download = true; return true; }
        if (download && InChain(key)) return false;   // already settled: nothing new
        Prune(now); downloads.push_back({key, now}); return false;
    }
    // The engine asks whether the site may download several files (PermissionRequested MULTIPLE_AUTOMATIC_DOWNLOADS,
    // for the site's origin): the navigation waits for that answer, it is a download, never an error. True when it
    // settles the held failure.
    bool DownloadAsked(const std::wstring& uri, std::uint64_t now) {
        const auto origin = Origin(uri); if (origin.empty()) return false;
        if (std::find(questions.begin(), questions.end(), origin) == questions.end()) questions.push_back(origin);
        if (held && !download && now <= failedAt + WaitMs && OriginInChain(origin)) { held = false; download = asked = true; return true; }
        if (download) return false;
        Prune(now); downloads.push_back({origin, now, true}); return false;
    }
    // A page was shown (NavigationCompleted success): the engine's question state starts over with it.
    void Shown() { questions.clear(); }
    bool Pending(std::uint64_t navigationId) const { return held && navigationId == id; }
    // The wait is over with no download: true once, the caller shows the error page then.
    bool Expire(std::uint64_t navigationId) { if (!Pending(navigationId)) return false; held = false; return true; }
};

}  // namespace NativeChrome::DownloadNavigation
