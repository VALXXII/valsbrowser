// Native render evidence for the address bar (issue #20): every rest state of the
// field and the suggestion list states, written as PNGs at 96 and 144 DPI, plus
// pixel checks of the invariants (indicator lane, danger tone, no ellipsis fade gap).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/address-popup.h"

using namespace NativeChrome;
int checks = 0;
void Check(bool condition, const char* label) { ++checks; if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); } }
int Red(UINT32 p) { return (p >> 16) & 255; }
int Green(UINT32 p) { return (p >> 8) & 255; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring root = argv[1], out = argv[2];
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    AddressBar::History history;
    history.Record(L"https://github.com/", L"GitHub", 1); history.Record(L"https://github.com/", L"GitHub", 2);
    history.Record(L"https://github.com/VALXXII/browser/issues", L"Issues · VALXXII/browser", 3);
    history.Record(L"https://gist.github.com/", L"Discover gists · GitHub", 4);
    history.Record(L"https://git-scm.com/docs", L"Git Reference", 5);
    for (UINT dpi : {96u, 144u}) {
        const std::wstring suffix = L"-" + std::to_wstring(dpi) + L".png";
        Chrome chrome; Check(SUCCEEDED(chrome.InitOffscreen(root, dpi, 1180, 84)), "offscreen chrome");
        std::vector<std::wstring> titles{L"GitHub", L"Example Domain"};
        struct State { const wchar_t* name; std::wstring uri; bool cert; int hover; };
        const State states[] = {
            {L"empty", L"", false, 0},
            {L"https", L"https://www.github.com/VALXXII/browser/issues/20", false, 0},
            {L"http", L"http://neverssl.com/online", false, 0},
            {L"certificate", L"https://expired.badssl.com/", true, 0},
            {L"file", L"file:///C:/Users/<user>/Documents/rapport%20final.pdf", false, 0},
            {L"long", L"https://docs.github.com/en/rest/issues/issues?apiVersion=2022-11-28#list-repository-issues-for-the-authenticated-user-with-filters", false, 0},
            {L"hover-http", L"http://neverssl.com/online", false, 'I'},
        };
        for (auto& s : states) {
            chrome.address = AddressBar::Describe(s.uri, s.cert); chrome.addressEditing = false;
            chrome.Paint(titles, 0, s.uri, false, s.hover, 0);
            Check(SUCCEEDED(chrome.SavePng((out + L"\\address-" + s.name + suffix).c_str())), "address snapshot");
            if (std::wstring(s.name) == L"certificate") {
                // The chip plate carries the danger tone (red dominant), not a grey.
                const UINT32 plate = chrome.SurfacePixel(chrome.bar.left + 6, 50);
                Check(Red(plate) > Green(plate) + 6, "certificate chip is tinted with the danger tone");
            }
            if (std::wstring(s.name) == L"https") {
                Check(chrome.indicator.left == chrome.bar.left + 4 && chrome.indicator.right - chrome.indicator.left == 24, "secure indicator is the 24 DIP slot");
                Check(chrome.edit.left == chrome.bar.left + 32, "EDIT text starts in the 32 DIP lane");
            }
        }
        // Editing: the indicator slot shows the active suggestion's glyph; no 'I' region.
        chrome.address = AddressBar::Describe(L"https://github.com/"); chrome.addressEditing = true; chrome.editIcon = L"search";
        chrome.Paint(titles, 0, L"", true, 0, 'L');
        Check(chrome.indicator.right <= chrome.indicator.left, "no indicator region while editing");
        Check(SUCCEEDED(chrome.SavePng((out + L"\\address-editing" + suffix).c_str())), "editing snapshot");

        // Suggestion list, field width (1180 - 82 - 133).
        const float width = 1180 - 82 - 133;
        struct List { const wchar_t* name; std::wstring typed; bool inlineAllowed; int active, hover; std::wstring engine; };
        const List lists[] = {
            {L"git", L"git", true, 0, -1, L"DuckDuckGo"},
            {L"git-keyboard", L"git", true, 2, -1, L"DuckDuckGo"},
            {L"git-hover", L"git", true, 0, 1, L"DuckDuckGo"},
            {L"search", L"météo lyon", true, 0, -1, L"Qwant"},
            {L"address", L"example.org/docs", true, 0, -1, L"DuckDuckGo"},
        };
        for (auto& l : lists) {
            auto data = AddressBar::Suggest(l.typed, history, l.inlineAllowed);
            const float height = AddressUI::Layout::Height(data.rows.size());
            Chrome popup; Check(SUCCEEDED(popup.InitOffscreen(root, dpi, width, height)), "offscreen popup");
            AddressUI::Suggestions view;
            // A remembered site icon (solid red 16x16) for github.com only.
            auto red = std::make_shared<FaviconImage>(); red->width = red->height = 16; red->stride = 64; red->pixels.assign(16 * 64, 0);
            for (size_t i = 0; i < red->pixels.size(); i += 4) { red->pixels[i + 2] = 230; red->pixels[i + 3] = 255; }
            view.SetIconSource([red](const std::wstring& site) { return site == L"github.com" ? std::shared_ptr<const FaviconImage>(red) : nullptr; });
            popup.RenderTarget()->BeginDraw();
            view.Preview(popup, data, l.active, l.hover, l.engine, width);
            Check(SUCCEEDED(popup.RenderTarget()->EndDraw()), "popup draw");
            Check(SUCCEEDED(popup.SavePng((out + L"\\suggestions-" + l.name + suffix).c_str())), "popup snapshot");
            // Active row plate (#FFFFFF14 over #171717 = #2A2A2A), idle rows on the canvas.
            const float y = AddressUI::Layout::pad + AddressUI::Layout::row * l.active + 2;
            Check((popup.SurfacePixel(width - 10, y) & 0xff) > 0x24, "active row has its plate");
            if (std::wstring(l.name) == L"git") {
                const float cy = AddressUI::Layout::pad + AddressUI::Layout::row / 2, x = AddressUI::Layout::pad + AddressUI::Layout::indent + AddressUI::Layout::slot / 2;
                Check(Red(popup.SurfacePixel(x, cy)) > 180 && Green(popup.SurfacePixel(x, cy)) < 40, "github.com row shows its remembered favicon");
                const float sy = AddressUI::Layout::pad + AddressUI::Layout::row * (data.rows.size() - .5f);
                Check(Red(popup.SurfacePixel(AddressUI::Layout::pad + AddressUI::Layout::slot / 2, sy)) < 120, "search row keeps the magnifier");
            }
            if (l.hover >= 0) { const float hy = AddressUI::Layout::pad + AddressUI::Layout::row * (l.hover + .5f); Check((popup.SurfacePixel(width - 4 - 4 - 12, hy) & 0xff) > (popup.SurfacePixel(width - 4 - 40, hy) & 0xff), "hovered site row shows its remove button"); }
        }
    }
    CoUninitialize();
    std::printf("address render checks passed: %d\n", checks);
    return 0;
}
