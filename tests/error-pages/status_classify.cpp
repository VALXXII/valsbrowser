// tests/error-pages: the page the browser shows for what the engine reported, with the very functions main.cpp
// calls (src/error-pages.h): NavigationFailed reads the engine's error document when the status is UNKNOWN without
// an HTTP status (EngineErrorCode / RefineUnknown), then HttpsUpgradeFailed (UpgradeFallsBack), KeepsServerPage,
// Classify.
//   status_classify.exe --script                                      the production script, for the probe host
//   status_classify.exe <WebErrorStatus> <HttpStatusCode> <script result JSON or -> [upgrade]   the page's name
#include "../../src/error-pages.h"
#include <cstdio>
#include <cwchar>

using namespace NativeChrome::ErrorPages;
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wcscmp(argv[1], L"--script") == 0) {
        const std::wstring s = EngineErrorCodeScript();
        for (wchar_t c : s) std::putchar(static_cast<char>(c));   // ASCII only
        return 0;
    }
    if (argc < 4) return 2;
    int status = static_cast<int>(std::wcstol(argv[1], nullptr, 10)); const int http = static_cast<int>(std::wcstol(argv[2], nullptr, 10));
    const std::wstring code = std::wcscmp(argv[3], L"-") == 0 ? L"" : EngineErrorCode(argv[3]);
    const bool upgrade = argc > 4 && std::wcscmp(argv[4], L"upgrade") == 0;
    if (status == COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN && http == 0) status = RefineUnknown(status, http, code);
    if (upgrade && UpgradeFallsBack(status, http)) { std::printf("Insecure %ls\n", code.c_str()); return 0; }
    if (KeepsServerPage(false, status, http)) { std::printf("ServerPage\n"); return 0; }
    static const char* names[] = {"Offline", "NameNotResolved", "Refused", "TimedOut", "Reset", "Unreachable", "Generic", "Certificate"};
    std::printf("%s %ls\n", names[static_cast<int>(Classify(status, true))], code.empty() ? Code(status) : code.c_str());
    return 0;
}
