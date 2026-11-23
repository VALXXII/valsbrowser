// browser.log privacy (src/log-privacy.h, audit SEC-05): origins only, capped file, erased by « Effacer les données ».
// Temporary folders only. Compiled with -Wall -Wextra -Werror (tests/update-integration/run_all.py).
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../src/log-privacy.h"

namespace fs = std::filesystem;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); }
static void Same(const std::wstring& in, const std::wstring& want, const char* what) {
    const std::wstring got = LogPrivacy::Redact(in);
    Check(got == want, what);
    if (got != want) std::printf("     got: %ls\n     want: %ls\n", got.c_str(), want.c_str());
}

int main() {
    Same(L"NAVIGATION OK https://mail.example.com/inbox?token=secret#msg", L"NAVIGATION OK https://mail.example.com", "path, query and fragment dropped");
    Same(L"HISTORY open https://user:pw@site.test:8443/a/b (new tab)", L"HISTORY open https://site.test:8443 (new tab)", "credentials dropped, port and trailing text kept");
    Same(L"HISTORY removed site.test", L"HISTORY removed site.test", "a bare host is kept");
    Same(L"NAVIGATION OK http://127.0.0.1:5184/page.html", L"NAVIGATION OK http://127.0.0.1:5184", "IP origin");
    Same(L"NAVIGATION OK file:///C:/Users/me/Documents/tax.pdf", L"NAVIGATION OK file://", "a local file keeps no path");
    Same(L"NAVIGATION OK data:text/html,<b>private text</b> tail", L"NAVIGATION OK data:", "data: keeps its scheme only (the rest of the line goes)");
    Same(L"NAVIGATION OK javascript:alert(document.cookie + 1)", L"NAVIGATION OK javascript:", "javascript: keeps its scheme only");
    Same(L"AUDIO artwork unavailable blob:https://site.test/1234-abcd", L"AUDIO artwork unavailable blob:https://site.test", "blob: keeps the inner origin");
    Same(L"HTTPS interstitial http://a.test/x then https://b.test/y?z", L"HTTPS interstitial http://a.test then https://b.test", "every address in a line");
    Same(L"SETTINGS adblock source: C:\\Program Files\\x stage=2 hr=0x80004005", L"SETTINGS adblock source: C:\\Program Files\\x stage=2 hr=0x80004005", "lines without an address are unchanged");
    Same(L"NAVIGATION FAILED WebErrorStatus=12 HTTPS://Site.Test/Path", L"NAVIGATION FAILED WebErrorStatus=12 HTTPS://Site.Test", "upper-case scheme");
    Same(L"x=https://a.test/p", L"x=https://a.test", "after =");
    Same(L"", L"", "empty line");

    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const fs::path work = fs::path(temp) / (L"log-privacy-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(work); fs::create_directories(work);
    const std::wstring log = (work / L"browser.log").wstring();
    { std::ofstream(fs::path(log), std::ios::binary) << std::string(100, 'a'); }
    LogPrivacy::RotateIfLarge(log, 1000);
    Check(fs::exists(log) && !fs::exists(log + L".old"), "a small log is not rotated");
    { std::ofstream(fs::path(log), std::ios::binary) << std::string(2000, 'b'); }
    LogPrivacy::RotateIfLarge(log, 1000);
    Check(!fs::exists(log) && fs::file_size(log + L".old") == 2000, "a log past the cap moves to .old");
    { std::ofstream(fs::path(log), std::ios::binary) << std::string(3000, 'c'); }
    LogPrivacy::RotateIfLarge(log, 1000);
    Check(!fs::exists(log) && fs::file_size(log + L".old") == 3000, "only one previous generation is kept");
    { std::ofstream(fs::path(log), std::ios::binary) << "x"; }
    Check(LogPrivacy::Erase(log) && !fs::exists(log) && !fs::exists(log + L".old"), "erase removes the log and its previous generation");
    Check(LogPrivacy::Erase(log), "erasing an absent log succeeds");
    Check(LogPrivacy::Erase((work / L"missing" / L"browser.log").wstring()), "erasing in a missing folder succeeds");
    fs::remove_all(work);

    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
