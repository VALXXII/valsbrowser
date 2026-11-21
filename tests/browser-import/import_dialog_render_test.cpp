// NATIVE FIXTURE (not the real app): the production modal component (src/modal.h Modal::Dialog) rendered OFFSCREEN
// with its own RenderForTest (Direct2D into a bitmap, no window capture, works with the display off or locked) for
// the import states, in French and English. Used when PrintWindow of the real app returns black (see
// design/browser-data-import-report.md « Captures »). The dialog content mirrors src/browser-import-integration.h
// (ImportChecks, ShowImportDialog, ImportFinished's summary), built from the same catalogue and BrowserImport
// helpers; that construction is duplicated here, so this proves the component's layout of that content, not the
// app's wiring (the real-app UIA E2E proves the wiring).
// Every PNG is decoded again and must be non-blank (lit pixels, several colours, light text present): a black or
// empty render fails, it can never pass silently.
// Usage: import_dialog_render_test.exe <repo root> <output folder>
#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/modal.h"
#include "../../src/browser-import.h"

using namespace NativeChrome;
namespace BI = BrowserImport;
namespace MD = Modal;
static int failures = 0, checks = 0;
static void Check(bool ok, const std::string& what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); }

// Lit pixels (max channel > 40), light pixels (> 200: text), distinct colours (capped), size.
struct Quality { UINT w = 0, h = 0; std::size_t lit = 0, light = 0, distinct = 0; bool read = false; };
static Quality Measure(const std::wstring& png) {
    Quality q; IWICImagingFactory* f = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(f->CreateFormatConverter(&cv)) && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) && SUCCEEDED(cv->GetSize(&q.w, &q.h))) {
        std::vector<BYTE> px(static_cast<std::size_t>(q.w) * q.h * 4);
        if (SUCCEEDED(cv->CopyPixels(nullptr, q.w * 4, static_cast<UINT>(px.size()), px.data()))) {
            q.read = true; std::vector<UINT> seen;
            for (std::size_t i = 0; i < px.size(); i += 4) {
                const BYTE m = std::max(px[i], std::max(px[i + 1], px[i + 2]));
                if (m > 40) ++q.lit; if (m > 200) ++q.light;
                const UINT c = px[i] | px[i + 1] << 8 | px[i + 2] << 16;
                if (seen.size() < 512 && std::find(seen.begin(), seen.end(), c) == seen.end()) seen.push_back(c);
            }
            q.distinct = seen.size();
        }
    }
    if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release(); if (f) f->Release();
    return q;
}

// Mirrors ImportChecks (src/browser-import-integration.h).
static std::vector<MD::Check> ImportChecks(const BI::Source& s) {
    MD::Check bookmarks, history, cookies;
    bookmarks.label = TR(import_item_bookmarks); history.label = TR(import_item_history); cookies.label = TR(import_item_cookies);
    bookmarks.value = s.bookmarks; bookmarks.disabled = !s.bookmarks;
    bookmarks.detail = s.bookmarks ? TRF(import_bookmarks_detail, s.browser == BI::Browser::HtmlFile ? std::wstring(TR(import_source_file_short)) : std::wstring(BI::BrandName(s.browser))) : std::wstring(TR(import_bookmarks_missing));
    if (s.browser == BI::Browser::HtmlFile) { history.disabled = true; history.detail = TR(import_history_html); }
    else { history.value = s.history; history.disabled = !s.history; history.detail = s.history ? TR(import_history_detail) : TR(import_history_missing); }
    cookies.disabled = true; cookies.detail = TR(import_cookies_unavailable);
    return {bookmarks, history, cookies};
}
// Mirrors ShowImportDialog.
static MD::Spec DialogSpec(const std::vector<BI::Source>& sources, int index, const std::wstring& profile) {
    MD::Spec s; s.kind = MD::Kind::Generic; s.scope = MD::Scope::Window; s.automationId = L"ImportBrowserData";
    s.title = TR(import_title); s.body = TRF(import_destination, profile);
    s.hasSelect = true; s.select.label = TR(import_source_label); s.select.index = index;
    for (const auto& src : sources) s.select.options.push_back(BI::Label(src));
    s.checks = ImportChecks(sources[static_cast<std::size_t>(index)]);
    s.buttons = {MD::Make(MD::CancelButton, TR(common_cancel), MD::Role::Secondary, false, true, false, L"DialogCancel"),
                 MD::Make(MD::AcceptButton, TR(import_action), MD::Role::Primary, true, false, true, L"DialogImport")};
    s.buttons[1].closes = false;
    return s;
}
// Mirrors ImportFinished's summary.
static MD::Spec SummarySpec(bool partial, const std::vector<std::wstring>& lines) {
    MD::Spec r; r.kind = MD::Kind::Generic; r.scope = MD::Scope::Window; r.automationId = L"ImportResult";
    r.title = partial ? TR(import_partial_title) : TR(import_done_title);
    for (const auto& l : lines) { if (!r.body.empty()) r.body += L"\n"; r.body += l; }
    r.buttons = {MD::Make(MD::AcceptButton, TR(common_ok), MD::Role::Primary, true, true, true, L"DialogOk")};
    return r;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: import_dialog_render_test <repo root> <output folder>\n"); return 2; }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    const std::wstring root = argv[1], out = argv[2];
    CreateDirectoryW(out.c_str(), nullptr);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"ImportRenderOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_POPUP, 100, 100, 1100, 860, nullptr, nullptr, hi, nullptr);
    MD::Dialog d; Check(d.Create(hi, owner, root, {}), "Dialog::Create");

    // A blank render must fail this check (self-test of the assertion with a deliberately black PNG).
    {
        Chrome blank; const bool made = SUCCEEDED(blank.InitOffscreen(root, 96, 400, 200)) && SUCCEEDED(blank.SavePng((out + L"\\_black-selftest.png").c_str()));
        const Quality q = Measure(out + L"\\_black-selftest.png");
        Check(made && q.read && q.light == 0, "quality self-test: an unpainted offscreen PNG is measured as blank (lit=" + std::to_string(q.lit) + ")");
        DeleteFileW((out + L"\\_black-selftest.png").c_str());
    }

    auto render = [&](MD::Spec spec, const std::wstring& name, const std::wstring& error = L"") {
        int done = 0; d.Show(std::move(spec), RECT{100, 100, 1100, 800}, [&](const MD::Result&) { ++done; });
        d.FinishAnimationsForTest();
        if (!error.empty()) d.SetError(error);
        for (UINT dpi : {96u, 144u}) {
            const std::wstring png = out + L"\\" + name + L"-" + std::to_wstring(dpi) + L".png";
            const bool ok = d.RenderForTest(png, dpi);
            const Quality q = Measure(png);
            char line[256]; std::snprintf(line, sizeof line, "%ls-%u: rendered, %ux%u lit=%zu light=%zu distinct=%zu", name.c_str(), dpi, q.w, q.h, q.lit, q.light, q.distinct);
            Check(ok && q.read && q.w > 0 && q.lit > static_cast<std::size_t>(q.w) * q.h / 50 && q.light > 200 && q.distinct > 8, line);
        }
        d.Abort(); d.FinishAnimationsForTest();
    };
    auto chrome = [](bool sole) { BI::Source s; s.browser = BI::Browser::Chrome; s.profile = L"Default"; s.isDefault = true; s.soleProfile = sole; s.bookmarks = s.history = true; return s; };
    BI::Source edge; edge.browser = BI::Browser::Edge; edge.profile = L"Profile 1"; edge.bookmarks = true; edge.soleProfile = true;
    BI::Source ff; ff.browser = BI::Browser::Firefox; ff.profile = L"default-release"; ff.bookmarks = ff.history = true;
    const std::vector<BI::Source> sources{chrome(true), edge, ff, BI::HtmlSource(L"")};
    BI::Counts bm; bm.imported = 6; bm.unsupported = 1; BI::Counts hist; hist.imported = 2; hist.unsupported = 2;
    BI::Counts big; big.imported = 9968; big.full = 170032;

    for (I18n::Locale l : {I18n::Locale::Fr, I18n::Locale::En}) {
        I18n::SetForProcess(l);
        const std::wstring lang = l == I18n::Locale::Fr ? L"fr" : L"en";
        render(DialogSpec(sources, 0, L"Personnel"), L"native-" + lang + L"-1-dialog-chrome");
        render(DialogSpec(sources, 1, L"Personnel"), L"native-" + lang + L"-2-dialog-edge-no-history");
        render(DialogSpec(sources, 3, L"Personnel"), L"native-" + lang + L"-3-dialog-html");
        render(DialogSpec(sources, 0, L"Personnel"), L"native-" + lang + L"-4-source-busy", TRF(import_result_failed, TR(import_item_history), BI::FailureText(BI::Failure::Busy, sources[0])));
        render(SummarySpec(false, {TRF(import_result_bookmarks, BI::CountsText(bm)), TRF(import_result_history, BI::CountsText(hist)), TR(import_result_cookies)}), L"native-" + lang + L"-5-summary-complete");
        render(SummarySpec(true, {TRF(import_result_failed, TR(import_item_bookmarks), TR(import_save_failed)), TRF(import_result_history, BI::CountsText(hist)), TR(import_result_cookies)}), L"native-" + lang + L"-6-summary-partial-save-failure");
        render(SummarySpec(true, {TRF(import_result_bookmarks, BI::CountsText(big)), TR(import_result_cookies)}), L"native-" + lang + L"-7-summary-partial-capacity");
        render(SummarySpec(false, {TRF(import_result_failed, TR(import_item_bookmarks), BI::FailureText(BI::Failure::TooDeep, sources[0]))}), L"native-" + lang + L"-8-too-deep");
    }
    I18n::SetForProcess(I18n::Locale::Fr);
    std::printf("%d checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    ExitProcess(failures ? 1 : 0);
}
