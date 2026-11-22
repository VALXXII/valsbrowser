// How long the onboarding's import-source listing takes on this machine: the real catalog code path of
// src/onboarding-import.h (registered browsers, profile folders' metadata, logos read as icon resources).
// Read-only, like the product's discovery: no bookmark, history or other content of any profile is opened, and
// nothing is printed but counts and milliseconds. This is the work that used to run on the UI thread when board 3
// opened and now runs on a worker thread (src/onboarding.h RequestSources).
// Usage: catalog_timing.exe [synthetic sources folder]   (none: this machine's own browsers)
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include "../../src/onboarding-import.h"

namespace BI = BrowserImport; namespace OI = OnboardingImport;

int wmain(int argc, wchar_t** argv) {
    const std::wstring synthetic = argc > 1 ? argv[1] : L"";
    for (int pass = 0; pass < 3; ++pass) {
        LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
        const BI::Roots roots = synthetic.empty() ? BI::DefaultRoots() : BI::Roots{synthetic + L"\\Local", synthetic + L"\\Roaming"};
        const auto registered = synthetic.empty() ? BI::ReadStartMenuInternet() : BI::ReadRegisteredFile(synthetic + L"\\startmenuinternet.txt");
        int logos = 0;
        const OI::Listing listing = OI::List(BI::Catalog(registered, BI::Discover(roots), L""), [&](const BI::Row& r) { auto image = BI::LoadLogo(r.icon, r.iconIndex, 96); if (image) ++logos; return image; });
        QueryPerformanceCounter(&b);
        std::printf("pass %d: %.1f ms (%zu entries, %zu profiles, %d logos)\n", pass + 1, (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart, listing.shown.size(), listing.profiles, logos);
    }
    return 0;
}
