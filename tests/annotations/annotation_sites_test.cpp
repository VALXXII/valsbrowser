// Where « Annoter » is on (#125, src/annotation-sites.h): local projects and local files, in a normal or a private
// window; live sites off; a page that leaves its local project for a live site loses the mode. Pure: no window.
// Usage: annotation_sites_test.exe
#include <cstdio>
#include <string>
#include "../../src/annotation-sites.h"

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

namespace S = AnnotationSites;
using P = S::Place;

int main() {
    // ---- on: local projects ----
    for (const char* url : {"http://localhost/", "http://localhost:5173/", "https://localhost:8443/app?x=1#y", "HTTP://LocalHost:3000/",
                            "http://127.0.0.1:5173/checkout", "http://127.0.0.1/", "http://127.42.7.9:8080/", "http://[::1]:3000/", "http://[0:0:0:0:0:0:0:1]/",
                            "http://shop.localhost:5173/", "http://my-app.dev.localhost/"}) {
        std::string what = std::string("on, normal and private window: ") + url;
        Expect(S::Of(url) == P::Local && S::Allowed(url, false) && S::Allowed(url, true), what.c_str());
    }
    // ---- on: local files ----
    for (const char* url : {"file:///C:/Users/val/projet/index.html", "file:///C:/Users/val/Mon%20site/a.html", "file://localhost/C:/x/index.html"}) {
        std::string what = std::string("on, local file: ") + url;
        Expect(S::Of(url) == P::LocalFile && S::Allowed(url, false) && S::Allowed(url, true), what.c_str());
    }
    // ---- off: live sites, look-alikes, anything else ----
    for (const char* url : {"https://example.com/", "https://www.lemonde.fr/planete/", "http://192.168.1.20:5173/", "http://10.0.0.5/", "http://0.0.0.0:3000/",
                            "http://localhost.example.com/", "http://127.0.0.1.example.com/", "http://0x7f.0.0.1/", "http://2130706433/", "http://127.1/",
                            "http://user@localhost:5173/", "http://localhost:99999/", "http://[::2]/", "https://xn--localhost-.com/"}) {
        std::string what = std::string("off, normal and private window: ") + url;
        Expect(!S::Allowed(url, false) && !S::Allowed(url, true), what.c_str());
    }
    Expect(S::Of("https://example.com/") == P::Online && S::Of("http://localhost.example.com/") == P::Online, "a live site is « Online » (the button says why it is off)");
    for (const char* url : {"file://server/share/index.html", "about:blank", "data:text/html,<p>x</p>", "javascript:void(0)", "edge://settings", "", "localhost:5173"}) {
        std::string what = std::string("off, not a local page: ") + (*url ? url : "(empty)");
        Expect(S::Of(url) == P::Other && !S::Allowed(url, false) && !S::Allowed(url, true), what.c_str());
    }
    // ---- private window: the choice in one place ----
    Expect(!S::OnlineInPrivateWindow, "private window: same rule as anywhere else (Val, 2026-11-24)");
    Expect(S::AllowedWith("https://example.com/", true, true) && !S::AllowedWith("https://example.com/", false, true), "private window: OnlineInPrivateWindow = true would open live sites there only");
    Expect(S::AllowedWith("http://localhost:5173/", true, false) && S::AllowedWith("file:///C:/a.html", true, false), "private window: local pages and files are never turned off for being private");
    // ---- navigation while the mode is on: the host asks again for the new page ----
    const char* path[] = {"http://localhost:5173/", "http://localhost:5173/merci", "https://example.com/", "http://localhost:5173/"};
    const bool expected[] = {true, true, false, true};
    bool same = true;
    for (int i = 0; i < 4; ++i) same = same && S::Allowed(path[i], false) == expected[i] && S::Allowed(path[i], true) == expected[i];
    Expect(same, "navigation: local → local stays on, local → live site turns the mode off, back to local may start again");
    Expect(S::Allowed("file:///C:/p/index.html", false) && !S::Allowed("https://cdn.example.com/page.html", false), "navigation: a local file → a live site turns the mode off");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
