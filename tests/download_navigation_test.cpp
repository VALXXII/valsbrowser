// A top-level navigation that the engine turns into a download (src/download-navigation.h): its failure
// (CONNECTION_ABORTED / OPERATION_CANCELED) is a download, never an error page, whatever the order of
// NavigationCompleted and DownloadStarting; a genuine abort, or an unrelated download, keeps the error page.
#include "../src/download-navigation.h"
#include <cstdio>
#include <cstdlib>

namespace DN = NativeChrome::DownloadNavigation;
static int failures = 0;
static void Require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; } }
constexpr int Aborted = 9, Canceled = 14, Unknown = 0, Unreachable = 6, Reset = 10, Refused = 12;

int main() {
    {   // NavigationCompleted first, then DownloadStarting (the order seen in the user's log): a short link redirects to the file
        DN::Tracker t;
        t.Starting(5, L"https://site.example/api/files/download", 1000);
        t.Starting(5, L"https://cdn.example/files/Ocular_Sounds.zip", 1010);   // server redirect: same navigation id
        Require(t.Failed(5, Aborted, 0, 1100) == DN::Verdict::Wait, "completion before the download: the verdict waits");
        Require(t.Pending(5), "the failure is held");
        Require(t.DownloadStarted(L"https://cdn.example/files/Ocular_Sounds.zip", 1120), "the download settles the held failure");
        Require(!t.Pending(5), "nothing is held any more");
        Require(!t.Expire(5), "the timer no longer shows an error page");
    }
    {   // DownloadStarting first, then NavigationCompleted
        DN::Tracker t;
        t.Starting(6, L"https://site.example/redir", 0);
        t.Starting(6, L"https://site.example/file.bin", 5);
        Require(!t.DownloadStarted(L"https://site.example/file.bin", 10), "nothing to settle yet: recorded");
        Require(t.Failed(6, Aborted, 0, 20) == DN::Verdict::Download, "completion after the download: a download");
        Require(!t.Expire(6), "no error page later");
    }
    {   // the download URL is the navigation's first hop (no redirect), fragment ignored, cancellation status
        DN::Tracker t;
        t.Starting(7, L"https://site.example/get?id=1#top", 0);
        Require(!t.DownloadStarted(L"https://site.example/get?id=1", 3), "recorded");
        Require(t.Failed(7, Canceled, 0, 4) == DN::Verdict::Download, "OPERATION_CANCELED with its download");
    }
    {   // UNKNOWN (a 200 response cut after its headers, a refused connection on some runtimes) is never held
        DN::Tracker t;
        t.Starting(8, L"https://site.example/cut", 0);
        Require(t.Failed(8, Unknown, 200, 1) == DN::Verdict::ErrorPage, "UNKNOWN + 200: error page at once");
        t.Starting(9, L"http://127.0.0.1:1/x", 2);
        Require(t.Failed(9, Unknown, 0, 3) == DN::Verdict::ErrorPage, "UNKNOWN + no response: error page at once");
    }
    {   // a genuine abort: no download comes, the timer shows the error page
        DN::Tracker t;
        t.Starting(9, L"https://site.example/page", 0);
        Require(t.Failed(9, Aborted, 0, 100) == DN::Verdict::Wait, "an abort waits for a possible download");
        Require(t.Expire(9), "no download: the error page is shown");
        Require(!t.Expire(9), "only once");
        Require(!t.Pending(9), "nothing held");
    }
    {   // failures a download never produces: the error page at once, never delayed
        for (int status : {Unreachable, Reset, Refused, 13 /* DNS */, 7 /* timeout */, 11 /* disconnected */}) {
            DN::Tracker t;
            t.Starting(10, L"https://site.example/page", 0);
            Require(t.Failed(10, status, 0, 1) == DN::Verdict::ErrorPage, "a transport failure is an error page at once");
        }
        DN::Tracker t;
        t.Starting(11, L"https://site.example/missing", 0);
        Require(t.Failed(11, Unknown, 404, 1) == DN::Verdict::ErrorPage, "an HTTP 4xx keeps its own path (the site's page)");
    }
    {   // an unrelated download in the same tab never swallows a genuine error
        DN::Tracker t;
        t.Starting(12, L"https://site.example/next", 0);
        Require(!t.DownloadStarted(L"https://cdn.example/other.zip", 5), "another URL: recorded, settles nothing");
        Require(t.Failed(12, Aborted, 0, 10) == DN::Verdict::Wait, "the earlier unrelated download does not count");
        Require(!t.DownloadStarted(L"blob:https://site.example/1234", 20), "a page-made download settles nothing");
        Require(t.Expire(12), "the genuine abort gets its error page");
    }
    {   // a download of the same URL from BEFORE this navigation started does not count
        DN::Tracker t;
        t.Starting(13, L"https://site.example/file.bin", 0);
        Require(t.Failed(13, Aborted, 0, 5) == DN::Verdict::Wait, "first try");
        Require(t.DownloadStarted(L"https://site.example/file.bin", 6), "a download");
        t.Starting(14, L"https://site.example/file.bin", 50000);   // much later, the same URL again
        Require(t.Failed(14, Aborted, 0, 50010) == DN::Verdict::Wait, "the old download does not settle the new navigation");
        Require(t.Expire(14), "error page without its own download");
    }
    {   // the download arrives after the wait: too late, the error page was already decided
        DN::Tracker t;
        t.Starting(15, L"https://site.example/file.bin", 0);
        Require(t.Failed(15, Aborted, 0, 10) == DN::Verdict::Wait, "waits");
        Require(t.Expire(15), "timer: error page");
        Require(!t.DownloadStarted(L"https://site.example/file.bin", 10 + DN::WaitMs + 500), "a late download settles nothing");
    }
    {   // a download recorded long before the completion (outside the pairing window) does not count
        DN::Tracker t;
        t.Starting(16, L"https://site.example/file.bin", 0);
        Require(!t.DownloadStarted(L"https://site.example/file.bin", 1), "recorded");
        Require(t.Failed(16, Aborted, 0, 1 + DN::WaitMs + 1) == DN::Verdict::Wait, "too far apart: not paired");
    }
    {   // a newer navigation replaces the held failure: its timer shows nothing
        DN::Tracker t;
        t.Starting(17, L"https://site.example/a", 0);
        Require(t.Failed(17, Aborted, 0, 5) == DN::Verdict::Wait, "held");
        t.Starting(18, L"https://site.example/b", 6);
        Require(!t.Pending(17) && !t.Expire(17), "an older navigation's timer is void");
        Require(t.Failed(17, Aborted, 0, 7) == DN::Verdict::ErrorPage, "an older navigation's failure keeps the existing path");
    }
    {   // a failure with no navigation seen (our own document, a sandbox page) keeps the existing path
        DN::Tracker t;
        Require(t.Failed(19, Aborted, 0, 1) == DN::Verdict::ErrorPage, "unknown navigation");
    }
    {   // the download settles only once: a second failure report of the same id is not a second download
        DN::Tracker t;
        t.Starting(20, L"https://site.example/f", 0);
        Require(!t.DownloadStarted(L"https://site.example/f", 1), "recorded");
        Require(t.Failed(20, Aborted, 0, 2) == DN::Verdict::Download, "a download");
        Require(t.Failed(20, Aborted, 0, 3) == DN::Verdict::Download, "the same navigation stays a download");
        Require(!t.DownloadStarted(L"https://site.example/f", 4), "a second download of it settles nothing new");
    }
    {   // a second download from the same page: the engine first asks « download multiple files? » (no DownloadStarting yet)
        DN::Tracker t;
        t.Starting(21, L"http://127.0.0.1:8080/files/download?name=b.bin", 0);
        Require(!t.DownloadAsked(L"http://127.0.0.1:8080/", 1), "question before the failure: recorded");
        Require(t.Failed(21, Aborted, 0, 2) == DN::Verdict::Download && t.asked, "the question makes it a download (asked)");
        DN::Tracker u;
        u.Starting(22, L"http://127.0.0.1:8080/files/download?name=c.bin", 0);
        Require(u.Failed(22, Aborted, 0, 2) == DN::Verdict::Wait, "held");
        Require(u.DownloadAsked(L"http://127.0.0.1:8080", 3) && u.asked, "question after the failure settles it");
        Require(!u.Expire(22), "no error page");
        Require(!u.DownloadStarted(L"http://127.0.0.1:8080/files/download?name=c.bin", 4000), "the download allowed later settles nothing new");
    }
    {   // a question from another site never settles this navigation
        DN::Tracker t;
        t.Starting(23, L"https://site.example/page", 0);
        Require(!t.DownloadAsked(L"https://other.example/", 1), "another origin: recorded");
        Require(t.Failed(23, Aborted, 0, 2) == DN::Verdict::Wait, "not paired");
        Require(!t.DownloadAsked(L"https://other.example/", 3), "still another origin");
        Require(t.Expire(23), "the error page");
        Require(DN::Origin(L"https://a.example:8443/x?y#z") == L"https://a.example:8443" && DN::Origin(L"about:blank").empty(), "origin");
    }
    {   // while the question is open (or after a refusal) the engine blocks that site's next downloads with no event at all
        DN::Tracker t;
        t.Starting(24, L"http://127.0.0.1:8080/files/download?name=b.bin", 0);
        Require(t.Failed(24, Aborted, 0, 1) == DN::Verdict::Wait, "held");
        Require(t.DownloadAsked(L"http://127.0.0.1:8080/", 2), "the question");
        t.Starting(25, L"http://127.0.0.1:8080/files/download?name=c.bin", 60000);   // a minute later, again
        Require(t.Failed(25, Aborted, 0, 60001) == DN::Verdict::Download && t.asked, "a blocked download of the asked site: no error page");
        t.Starting(26, L"https://other.example/x", 60002);
        Require(t.Failed(26, Aborted, 0, 60003) == DN::Verdict::Wait, "another site keeps its own verdict");
        t.Starting(27, L"http://127.0.0.1:8080/files/download?name=d.bin", 60004);
        Require(t.Failed(27, Reset, 0, 60005) == DN::Verdict::ErrorPage, "a reset is never a blocked download");
        t.Shown();   // a page was shown: the engine's question state starts over
        t.Starting(28, L"http://127.0.0.1:8080/files/download?name=e.bin", 60006);
        Require(t.Failed(28, Aborted, 0, 60007) == DN::Verdict::Wait, "after a shown page the old question no longer counts");
    }
    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::puts("download navigation tests passed");
    return 0;
}
