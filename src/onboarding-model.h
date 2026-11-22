#pragma once
// Onboarding — what the window and its host exchange about the import (board 3 and after). Plain data: the
// window (src/onboarding.h) shows it, the host (src/onboarding-import.h, src/onboarding-integration.h) fills it.
#include <memory>
#include <string>
#include <vector>
#include "favicon.h"

namespace NativeChrome {
namespace OnboardingUI {

struct BrowserProfile { std::wstring label; bool bookmarks = false, history = false; };
struct BrowserSource {
    std::wstring name;                                   // « Google Chrome »
    std::wstring detail;                                 // why it cannot be chosen (unavailable rows)
    std::shared_ptr<const FaviconImage> logo;
    std::vector<BrowserProfile> profiles;                // one entry per profile of that browser
    bool available = true, file = false;                 // file: « Fichier de favoris HTML… »
};
struct ImportPick { int browser = -1, profile = 0; bool bookmarks = true, history = true; };
// One item of the source's bookmarks bar, as the browser's own bar would show it (title, folder or link).
struct BarItem { std::wstring title; bool folder = false; };
struct ImportOutcome {
    bool bookmarksSaved = false, historySaved = false; std::wstring detail;   // detail: why the first failure happened
    std::vector<BarItem> bar;                                                  // bookmarks saved: what now sits in the bookmarks bar, in order
};

} // namespace OnboardingUI
} // namespace NativeChrome
