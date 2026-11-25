// tests/print: our print window (#121, src/print-dialog.h) and its preview pages (src/print-preview.h),
// without ever showing a window: the dialog's windows are created but never shown, focused or activated, and
// its pictures are offscreen Direct2D renders. Safe while the PC is in use (daytime rule).
//
//   print_dialog_test.exe <repo root> <output folder>
//
//  1. page ranges as the engine reads them (WebView2 PageRanges examples) and the sheet's shape;
//  2. the window's logic (headless): counts, pager, Toutes / Choisir… and the range field, orientation,
//     backgrounds, printer list, Échap / Entrée, exactly one answer per opening, UIA names;
//  3. the panel rendered offscreen at 96 and 120 DPI, compared pixel by pixel with Paper's export of the board
//     (tests/print/paper/modal-*.png; the sheet gets the board's own sheet as its page image);
//  4. the preview worker on generated PDFs: page count, the right page drawn, a 300-page document, no call
//     blocking the calling thread.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include "../../src/chrome.h"
#include "../../src/print-dialog.h"
#include "../../src/print-preview.h"

using namespace NativeChrome;
namespace PD = NativeChrome::PrintDialog;

// The UIA tree as a client walks it: one root (the window's group), every key once, every parent present. Release
// 25/11: « Page précédente » had the group's key, a UIA client looped window -> sheet -> window... (~1000 nodes, 9 s).
static bool TreeOk(const std::vector<Accessibility::Node>& nodes) {
    int roots = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parent == 0) ++roots;
        else if (std::none_of(nodes.begin(), nodes.end(), [&](const Accessibility::Node& p) { return p.key == nodes[i].parent; })) return false;
        for (size_t j = i + 1; j < nodes.size(); ++j) if (nodes[i].key == nodes[j].key) return false;
    }
    return roots == 1 && !nodes.empty() && nodes[0].parent == 0;
}
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAILED: %s\n", what); } }

// ---- images -------------------------------------------------------------------------------------------
struct Image { UINT w = 0, h = 0; std::vector<BYTE> px; };   // BGRA, straight or premultiplied as loaded (32bppPBGRA)
static bool Load(const std::wstring& path, Image& out) {
    IWICImagingFactory* f = nullptr; IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* c = nullptr; bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &d)) && SUCCEEDED(d->GetFrame(0, &fr)) &&
        SUCCEEDED(f->CreateFormatConverter(&c)) && SUCCEEDED(c->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(c->GetSize(&out.w, &out.h))) { out.px.resize(static_cast<size_t>(out.w) * out.h * 4); ok = SUCCEEDED(c->CopyPixels(nullptr, out.w * 4, static_cast<UINT>(out.px.size()), out.px.data())); }
    if (c) c->Release(); if (fr) fr->Release(); if (d) d->Release(); if (f) f->Release();
    return ok;
}
static bool Save(const std::wstring& path, const Image& im) {
    IWICImagingFactory* f = nullptr; IWICStream* s = nullptr; IWICBitmapEncoder* e = nullptr; IWICBitmapFrameEncode* fr = nullptr; bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && SUCCEEDED(f->CreateStream(&s)) &&
        SUCCEEDED(s->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e)) &&
        SUCCEEDED(e->Initialize(s, WICBitmapEncoderNoCache)) && SUCCEEDED(e->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) &&
        SUCCEEDED(fr->SetSize(im.w, im.h))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA; fr->SetPixelFormat(&fmt);
        ok = SUCCEEDED(fr->WritePixels(im.h, im.w * 4, static_cast<UINT>(im.px.size()), const_cast<BYTE*>(im.px.data()))) && SUCCEEDED(fr->Commit()) && SUCCEEDED(e->Commit());
    }
    if (fr) fr->Release(); if (e) e->Release(); if (s) s->Release(); if (f) f->Release();
    return ok;
}
static Image Crop(const Image& im, int x, int y, int w, int h) {
    Image o; o.w = static_cast<UINT>(w); o.h = static_cast<UINT>(h); o.px.resize(static_cast<size_t>(w) * h * 4);
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) for (int k = 0; k < 4; ++k) o.px[(static_cast<size_t>(j) * w + i) * 4 + k] = im.px[(static_cast<size_t>(y + j) * im.w + (x + i)) * 4 + k];
    return o;
}
// Pixels where Paper has an opaque pixel: share of channels apart by more than `tolerance`, and a diff image.
struct Diff { double share = 1, mean = 255; size_t compared = 0; };
static Diff Compare(const Image& ours, const Image& paper, int tolerance, Image* heat, int rows = -1) {
    Diff d; if (ours.w != paper.w) return d;
    const UINT h = std::min(ours.h, paper.h) - (rows > 0 && static_cast<UINT>(rows) < std::min(ours.h, paper.h) ? std::min(ours.h, paper.h) - static_cast<UINT>(rows) : 0);
    size_t off = 0; double sum = 0; d.compared = 0;
    if (heat) { heat->w = ours.w; heat->h = ours.h; heat->px.assign(ours.px.size(), 0); }
    // Paper's export sits about half a pixel off the integer grid (its edges are half-covered pixels where the
    // board's integer layout has none): a pixel matches when Paper has a close one within one pixel.
    for (size_t p = 0; p < static_cast<size_t>(ours.w) * h; ++p) {
        const int x = static_cast<int>(p % ours.w), y = static_cast<int>(p / ours.w);
        const BYTE* a = &ours.px[p * 4]; const BYTE* b = &paper.px[(static_cast<size_t>(y) * paper.w + x) * 4];
        if (b[3] < 255 || x < 2 || y < 2 || x >= static_cast<int>(ours.w) - 2 || y >= static_cast<int>(h) - 2) continue;   // corners and the 1 px border rim (sub-pixel in the export)
        ++d.compared; int worst = 0;
        for (int k = 0; k < 3; ++k) sum += std::abs(a[k] - b[k]);
        worst = 255;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const BYTE* c = &paper.px[(static_cast<size_t>(y + dy) * paper.w + (x + dx)) * 4]; int v = 0;
            for (int k = 0; k < 3; ++k) v = std::max(v, std::abs(a[k] - c[k]));
            worst = std::min(worst, v);
        }
        if (worst > tolerance) ++off;
        if (heat) { BYTE* h = &heat->px[p * 4]; const BYTE g = static_cast<BYTE>(b[1] / 3); h[0] = g; h[1] = g; h[2] = worst > tolerance ? 255 : g; h[3] = 255; }
    }
    d.share = d.compared ? static_cast<double>(off) / d.compared : 1; d.mean = d.compared ? sum / (d.compared * 3.0) : 255;
    return d;
}

// ---- a PDF made here: `pages` A4 pages, page i has one black bar whose top is 100 + 60 * (i % 10) pt --------------
static std::vector<BYTE> MakePdf(int pages) {
    std::string out = "%PDF-1.4\n"; std::vector<size_t> at;
    auto obj = [&](const std::string& body) { at.push_back(out.size()); out += std::to_string(at.size()) + " 0 obj\n" + body + "\nendobj\n"; };
    obj("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids; for (int i = 0; i < pages; ++i) kids += std::to_string(3 + 2 * i) + " 0 R ";
    obj("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) + " >>");
    for (int i = 0; i < pages; ++i) {
        obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents " + std::to_string(4 + 2 * i) + " 0 R /Resources << >> >>");
        const std::string draw = "0 0 0 rg 50 " + std::to_string(842 - 100 - 60 * (i % 10) - 40) + " 300 40 re f\n";
        obj("<< /Length " + std::to_string(draw.size()) + " >>\nstream\n" + draw + "endstream");
    }
    const size_t xref = out.size();
    out += "xref\n0 " + std::to_string(at.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t p : at) { char line[32]; std::snprintf(line, sizeof line, "%010zu 00000 n \n", p); out += line; }
    out += "trailer\n<< /Size " + std::to_string(at.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return std::vector<BYTE>(out.begin(), out.end());
}
static std::vector<PrintPreview::Result> WaitFor(PrintPreview::Renderer& r, size_t want, int ms, unsigned generation) {   // results of older documents are dropped, as the browser does
    std::vector<PrintPreview::Result> got; const auto end = GetTickCount64() + static_cast<ULONGLONG>(ms);
    while (got.size() < want && GetTickCount64() < end) { for (auto& x : r.Take()) if (x.generation == generation) got.push_back(std::move(x)); if (got.size() < want) Sleep(5); }
    return got;
}
static bool Dark(const PrintPreview::Result& r, float fx, float fy) {
    const UINT x = static_cast<UINT>(fx * r.width), y = static_cast<UINT>(fy * r.height); if (x >= r.width || y >= r.height) return false;
    const BYTE* p = &r.pixels[(static_cast<size_t>(y) * r.width + x) * 4]; return p[0] < 60 && p[1] < 60 && p[2] < 60 && p[3] > 200;
}

int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    const std::wstring root = argc > 1 ? argv[1] : L".", outDir = argc > 2 ? argv[2] : L".";
    I18n::SetForProcess(I18n::Locale::Fr);
    for (auto name : FontFiles) AddFontResourceExW((root + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);

    // 1. Ranges (WebView2 ICoreWebView2PrintSettings2::PageRanges, 20 pages) and the sheet.
    {
        std::vector<int> p;
        Check(PD::ParseRanges(L"2", 20, &p) && p == std::vector<int>{1}, "ranges: \"2\"");
        Check(PD::ParseRanges(L"1-4, 9, 3-6, 10, 11", 20, &p) && p == std::vector<int>({0, 1, 2, 3, 4, 5, 8, 9, 10}), "ranges: \"1-4, 9, 3-6, 10, 11\" -> 1-6, 9-11");
        Check(PD::ParseRanges(L"1-4, -6", 20, &p) && p.size() == 6 && p.back() == 5, "ranges: \"-6\" from page 1");
        Check(PD::ParseRanges(L"2-", 20, &p) && p.size() == 19 && p.front() == 1 && p.back() == 19, "ranges: \"2-\" to the end");
        Check(PD::ParseRanges(L"-", 20, &p) && p.size() == 20, "ranges: \"-\" every page");
        Check(PD::ParseRanges(L"2-2", 20, &p) && p == std::vector<int>{1}, "ranges: \"2-2\"");
        Check(!PD::ParseRanges(L"4-2, 11, -6", 20), "ranges: \"4-2\" refused");
        Check(!PD::ParseRanges(L"1-4dsf, 11", 20), "ranges: junk refused");
        Check(!PD::ParseRanges(L"21", 20) && !PD::ParseRanges(L"0", 20) && !PD::ParseRanges(L"", 20) && !PD::ParseRanges(L"1,", 20) && !PD::ParseRanges(L",1", 20), "ranges: out of the document, empty, stray commas refused");
        Check(PD::ParseRanges(L"1–3", 5, &p) && p.size() == 3 && PD::EngineRanges(L"1–3, 5") == L"1-3, 5", "ranges: typographic dash and no-break space accepted, handed to the engine as '-' and ' '");
        Check(!PD::ParseRanges(L"1", 0), "ranges: no document, no range");
        const auto a4 = PD::SheetIn({25, 25, 337, 421}, 210, 297, false);
        Check(a4.left == 41 && a4.top == 25 && a4.right == 321 && a4.bottom == 421, "sheet: A4 portrait is the board's 280 x 396 at (41, 25)");
        const auto land = PD::SheetIn({25, 25, 337, 421}, 210, 297, true);
        Check(land.left == 25 && land.right == 337 && std::fabs((land.bottom - land.top) - 221) < .5f && std::fabs(land.top - (25 + 88)) < .5f, "sheet: A4 landscape fills the slot's width, centred");
        const auto letter = PD::SheetIn({25, 25, 337, 421}, 215.9f, 279.4f, false);
        Check(letter.right - letter.left == 306 && letter.bottom - letter.top == 396, "sheet: Letter portrait 306 x 396");
    }

    // 2. Logic, headless (windows created, never shown).
    HINSTANCE hi = GetModuleHandleW(nullptr);
    struct Log { int changed = 0, accepted = 0, cancelled = 0, added = 0; PD::State last; std::vector<std::pair<int, std::pair<UINT, UINT>>> wants; } log;
    PD::Dialog::Hooks hooks;
    hooks.changed = [&](const PD::State& s) { ++log.changed; log.last = s; };
    hooks.want = [&](int page, UINT w, UINT h) { log.wants.push_back({page, {w, h}}); };
    hooks.accept = [&](const PD::State& s) { ++log.accepted; log.last = s; };
    hooks.cancel = [&] { ++log.cancelled; };
    hooks.addPrinter = [&] { ++log.added; };
    PD::Dialog d; Check(d.Create(hi, nullptr, root, hooks), "Create (hidden windows)");
    Check(!IsWindowVisible(d.Window()) && !IsWindowVisible(d.Scrim()), "nothing is shown by Create");
    using D = PD::Dialog;
    PD::State st; st.printers = {L"Enregistrer en PDF", L"QA & Laser"}; st.pdf = 0;
    const RECT area{0, 0, 1200, 800};
    {
        Check(d.OpenHeadlessForTest(st, area, 8), "open (headless)");
        Check(!IsWindowVisible(d.Window()) && !d.NarrowForTest(), "headless: still not shown, wide window");
        Check(d.SubtitleForTest() == L"Calcul des feuilles…" && d.PagerForTest() == L"Préparation de l’aperçu…", "state 5: « Calcul des feuilles… », « Préparation de l’aperçu… »");
        Check(!d.EnabledForTest(D::PreviousHit) && !d.EnabledForTest(D::NextHit) && d.EnabledForTest(D::AcceptHit), "state 5: no page to turn, the action is available");
        Check(d.FocusForTest() == D::AcceptHit, "initial focus: the primary button (Ctrl+P then Entrée prints)");
        Check(d.PrimaryForTest() == L"Enregistrer", "PDF destination: « Enregistrer »");
        log.wants.clear(); d.SetDocument(3);
        Check(d.SubtitleForTest() == L"3 feuilles" && d.PagerForTest() == L"Page 1 sur 3", "3 pages: « 3 feuilles », « Page 1 sur 3 »");
        Check(TreeOk(d.NodesForTest()), "UIA tree (sheet, pager): one root, unique keys, no loop");
        Check(!log.wants.empty() && log.wants.back().first == 0 && log.wants.back().second.first == 280 && log.wants.back().second.second == 396, "the sheet asks for page 1 at its own pixel size (280 x 396 at 96 DPI), last = served first");
        Check(!d.EnabledForTest(D::PreviousHit) && d.EnabledForTest(D::NextHit), "page 1: no previous page, a next one (board: left chevron #FFFFFF52)");
        d.ActivateForTest(D::NextHit); d.ActivateForTest(D::NextHit); d.ActivateForTest(D::NextHit);
        Check(d.PageIndexForTest() == 2 && d.PagerForTest() == L"Page 3 sur 3" && !d.EnabledForTest(D::NextHit), "pager: stops on the last page");
        d.KeyForTest(VK_PRIOR); Check(d.PageIndexForTest() == 1, "Page précédente key turns back");
        d.SetDocument(1); Check(d.SubtitleForTest() == L"1 feuille" && d.PagerForTest() == L"Page 1 sur 1", "1 page: singular « 1 feuille »");
        d.SetDocument(3);
        // State 1: « Choisir » shows every page as a ticked thumbnail; the field and the boxes follow each other.
        d.ActivateForTest(D::ChoosePagesHit, true);
        Check(d.FocusForTest() == D::RangeStop && !d.EnabledForTest(D::AcceptHit) && d.ModeForTest() == D::Mode::Grid, "Choisir: thumbnails, the field takes the focus, nothing ticked = no action yet");
        Check(d.RangeBoxForTest().top == 221 && d.BoxForTest(D::PortraitHit).top == 321 + 2, "Choisir: the field 8 under the choice, help line 6 under it, the next fields move down by 64");
        Check(d.BoxForTest(D::ThumbHit(0)).left == 49 && d.BoxForTest(D::ThumbHit(0)).top == 41 && d.BoxForTest(D::ThumbHit(1)).left == 193 && d.BoxForTest(D::ThumbHit(2)).top == 41 + 194 + 12, "state 1: thumbnails 120 x 170 in a 272 grid (columns 128, gap 16, rows 12), centred");
        Check(TreeOk(d.NodesForTest()), "UIA tree (thumbnails): one root, unique keys, no loop");
        d.SetRangeForTest(L"1-2");
        Check(d.TickedForTest() == std::vector<int>({0, 1}) && d.SubtitleForTest() == L"2 feuilles" && d.EnabledForTest(D::AcceptHit), "« 1-2 »: pages 1 and 2 ticked, « 2 feuilles »");
        d.ActivateForTest(D::ThumbHit(2));
        Check(d.TickedForTest() == std::vector<int>({0, 1, 2}) && d.Current().ranges == L"1-3", "ticking page 3 writes « 1-3 » in the field");
        d.ActivateForTest(D::ThumbHit(1));
        Check(d.Current().ranges == L"1, 3" && d.SubtitleForTest() == L"2 feuilles", "unticking page 2 writes « 1, 3 »");
        Check(PD::FormatRanges({0, 1, 2, 4}) == L"1-3, 5" && PD::FormatRanges({0, 1}) == L"1, 2" && PD::FormatRanges({}).empty(), "the field's text for ticked pages (« 1-3, 5 »)");
        // State 2: a page after the last one.
        d.SetRangeForTest(L"7");
        Check(d.RangeErrorForTest() && d.RangeBeyondForTest() && !d.EnabledForTest(D::AcceptHit) && d.ModeForTest() == D::Mode::Sheet && d.SubtitleForTest() == L"3 feuilles", "state 2: « 7 » of 3: field in error, action off, the sheet again, « 3 feuilles »");
        d.SetRangeForTest(L"2-x");
        Check(d.RangeErrorForTest() && !d.RangeBeyondForTest() && !d.EnabledForTest(D::AcceptHit), "malformed range: field in error without the « n’a que » message (not on the board)");
        d.SetRangeForTest(L"1, 3");
        d.ActivateForTest(D::AllPagesHit);
        Check(d.VisibleForTest().size() == 3 && d.EnabledForTest(D::AcceptHit) && log.changed == 0 && d.ModeForTest() == D::Mode::Sheet, "Toutes: every page again; ranges never ask for a new document");
        // State 3: landscape.
        d.ActivateForTest(D::LandscapeHit);
        Check(log.changed == 1 && log.last.landscape && d.SubtitleForTest() == L"Calcul des feuilles…", "Paysage: a new document is asked, the sheet waits (state 5)");
        d.SetDocument(3);
        const auto sheet = d.SheetForTest(); Check(sheet.right - sheet.left == 312 && std::fabs((sheet.bottom - sheet.top) - 221) < .5f && sheet.left == 25 && sheet.top == 113, "state 3: landscape sheet 312 x 221, the sheet and the pager centred in the column");
        d.ActivateForTest(D::BackgroundsHit); Check(log.changed == 2 && !log.last.backgrounds, "fonds: unchecking asks for a new document");
        d.SetDocument(3);
        // State 4: the printer list drawn in the window.
        d.ActivateForTest(D::PrinterHit, true);
        const auto texts = d.ListTextsForTest(); const auto list = d.ListForTest();
        Check(d.ListOpenForTest() && texts.size() == 2 && texts[0] == L"Enregistrer en PDF" && texts[1] == L"QA & Laser" && list.left == 376 && list.top == 140 && list.right - list.left == 328, "printer list: under the select (376, 140, 328 wide), PDF then the printers");
        Check(TreeOk(d.NodesForTest()), "UIA tree (printer list open): one root, unique keys, no loop");
        d.KeyForTest(VK_DOWN); d.KeyForTest(VK_RETURN);
        Check(!d.ListOpenForTest() && log.changed == 3 && log.last.printer == 1 && d.PrimaryForTest() == L"Imprimer", "↓ Entrée: the printer chosen, new document, « Imprimer » (state 3)");
        // State 6: no preview; it still prints.
        d.SetUnavailable();
        Check(d.ModeForTest() == D::Mode::Unavailable && d.SubtitleForTest().empty() && d.EnabledForTest(D::AcceptHit), "state 6: « Aperçu indisponible », no count, the action stays");
        d.SetDocument(3);
        // UIA.
        const auto nodes = d.NodesForTest(); bool names = nodes.size() >= 12;
        for (auto& n : nodes) if (n.automationId != L"PrintDialog" && n.automationId != L"PrintPageNumber" && n.name.empty()) names = false;
        Check(names, "UIA: every control has a name");
        d.KeyForTest(VK_TAB);
        Check(d.FocusForTest() != 0, "Tab moves the focus");
        d.ActivateForTest(D::AcceptHit, true);
        d.KeyForTest(VK_ESCAPE);
        Check(log.accepted == 1 && log.cancelled == 0 && !d.Visible() && log.last.landscape && !log.last.backgrounds && log.last.printer == 1, "accept: one answer with the chosen settings, Échap afterwards does nothing");
        d.FinishAnimationsForTest();
    }
    {   // Entrance fade (night 24/11: in a real window the panel and its scrim stayed at about 15 % opacity). The windows
        // are the hidden ones of the headless open; Frame() runs with the test's own times, as the browser's clock calls it.
        using namespace std::chrono_literals;
        Check(d.OpenHeadlessForTest(st, area, 8), "open for the entrance (headless)");
        const auto t0 = Motion::Now(); d.StartEntranceForTest(t0);
        Check(d.AlphaForTest() == 0 && d.ScrimAlphaForTest() == 0 && !d.SettledForTest(), "entrance: starts transparent, not settled");
        d.Frame(t0 + 8ms);
        const BYTE early = d.AlphaForTest(); const auto wake = d.NextWake(t0 + 8ms);
        Check(early > 0 && early < 128 && wake.frames, "entrance: the first frame is partly faded in and asks for more frames");
        // The UI thread was busy while the window opened (first paint, engine calls): the next frame comes after the
        // 150 ms fade. It must still land the full opacity, the scrim's 50 % and the final place.
        d.Frame(t0 + 400ms);
        const RECT r = d.PanelRectForTest();
        Check(d.AlphaForTest() == 255 && d.ScrimAlphaForTest() == 128, "entrance: a frame after the fade's end lands 100 % (panel) and 50 % (scrim)");
        Check(r.top == area.top + 59 && d.SettledForTest(), "entrance: the panel ends 59 under the page area's top (the 4 DIP rise gone)");
        const auto after = d.NextWake(t0 + 400ms);
        Check(!after.frames && !after.delay.has_value(), "entrance: settled, no more frames asked");
        // Frames on time all along end the same way.
        d.StartEntranceForTest(t0); for (int ms = 0; ms <= 160; ms += 16) d.Frame(t0 + std::chrono::milliseconds(ms));
        Check(d.AlphaForTest() == 255 && d.ScrimAlphaForTest() == 128 && d.SettledForTest(), "entrance: frames every 16 ms end at 100 % / 50 %");
        // The exit: Échap fades out and hides the windows once a frame passes the 120 ms.
        d.KeyForTest(VK_ESCAPE); d.Frame(Motion::Now() + 300ms);
        Check(!d.Visible() && !d.Closing() && !IsWindowVisible(d.Window()), "exit: a late frame still ends the fade-out");
    }
    {   // State 4 without printers; Échap closes the list first.
        PD::State none; none.printers = {L"Enregistrer en PDF"}; none.pdf = 0;
        Check(d.OpenHeadlessForTest(none, area, 8), "reopen without printers (headless)");
        d.SetDocument(4); d.ActivateForTest(D::PrinterHit, true);
        const auto texts = d.ListTextsForTest();
        Check(texts.size() == 3 && texts[1] == L"Aucune imprimante installée" && texts[2] == L"Ajouter une imprimante…", "state 4: « Aucune imprimante installée », « Ajouter une imprimante… »");
        d.KeyForTest(VK_ESCAPE); Check(!d.ListOpenForTest() && d.Visible(), "Échap closes the list, not the window");
        d.ActivateForTest(D::PrinterHit, true); d.ActivateForTest(D::ListHit(3));
        Check(log.added == 1 && !d.ListOpenForTest() && d.Visible(), "« Ajouter une imprimante… » opens Windows' printer settings, the window stays");
        d.KeyForTest(VK_ESCAPE); d.KeyForTest(VK_RETURN);
        Check(log.cancelled == 2 && log.accepted == 1 && !d.Visible(), "Échap: cancel, exactly once");
        d.FinishAnimationsForTest();
    }
    {   // State 1 with a long document: the grid scrolls (not on the board).
        Check(d.OpenHeadlessForTest(st, area, 8), "reopen for a long document");
        d.SetDocument(43); d.ActivateForTest(D::ChoosePagesHit, true);
        const float before = d.BoxForTest(D::ThumbHit(0)).top; d.WheelForTest(500);
        Check(before == 25 && d.BoxForTest(D::ThumbHit(0)).top < before, "43 pages: the grid starts at the top and scrolls with the wheel");
        d.Abort(); d.FinishAnimationsForTest();
    }
    {   // State 10: under 760 of page area, one column 400 wide.
        Check(d.OpenHeadlessForTest(st, RECT{0, 0, 740, 800}, 8), "open in a narrow page area");
        d.SetDocument(3);
        const auto size = d.SizeForTest(); const auto chip = d.ChipForTest();
        Check(d.NarrowForTest() && size.cx == 400 && size.cy == 434 && chip.right == 383 && chip.top == 17 && chip.bottom - chip.top == 52, "state 10: 400 x 434, the « Aperçu » chip in the header");
        Check(!d.EnabledForTest(D::PreviousHit) && log.wants.back().second.first == 32 && log.wants.back().second.second == 44, "state 10: no pager; the chip asks for page 1 at 32 x 44");
        d.Abort(); d.FinishAnimationsForTest();
    }
    {   // English labels.
        I18n::SetForProcess(I18n::Locale::En);
        Check(d.OpenHeadlessForTest(st, area, 8), "open (English)");
        d.SetDocument(3);
        Check(d.SubtitleForTest() == L"3 sheets" && d.PagerForTest() == L"Page 1 of 3" && d.PrimaryForTest() == L"Save", "English: 3 sheets, Page 1 of 3, Save");
        d.Abort(); d.FinishAnimationsForTest();
        I18n::SetForProcess(I18n::Locale::Fr);
    }

    // 3. Offscreen renders against Paper's exports of the boards (the sheets get the boards' own sheets as page images).
    {
        PD::State board; board.printers = {L"Enregistrer en PDF"}; board.pdf = 0;
        auto paperOf = [&](const wchar_t* name, Image& im) { return Load(root + L"\\tests\\print\\paper\\" + name, im); };
        auto compare = [&](const wchar_t* name, const Image& paper, int rows, double share, const char* what) {
            Image ours; Check(Load(outDir + L"\\" + name + L".png", ours), "render loads");
            Image heat; const auto diff = Compare(ours, paper, 40, &heat, rows); Save(outDir + L"\\" + name + L"-diff.png", heat);
            std::printf("  %ls vs Paper: %.2f %% of pixels off by more than 40, mean %.2f (%zu pixels)\n", name, diff.share * 100, diff.mean, diff.compared);
            Check(diff.share < share, what);
            return ours;
        };
        for (UINT dpi : {96u, 120u}) {
            const std::wstring suffix = dpi == 96 ? L"1x" : L"1.25x";
            Image paper; Check(paperOf((L"modal-" + suffix + L".png").c_str(), paper), "Paper export loads");
            const float k = dpi / 96.f;
            const int sx = static_cast<int>(std::lround(41 * k)), sy = static_cast<int>(std::lround(25 * k)), sw = static_cast<int>(std::lround(280 * k)), sh = static_cast<int>(std::lround(396 * k));
            Image page = Crop(paper, sx, sy, sw, sh);
            std::map<int, std::pair<SIZE, std::vector<BYTE>>> images; images[0] = {SIZE{sw, sh}, page.px};
            const std::wstring name = L"print-dialog-" + suffix;
            Check(d.RenderForTest(root, outDir + L"\\" + name + L".png", dpi, board, 3, 0, images), "offscreen render");
            const Image ours = compare(name.c_str(), paper, -1, .02, "the window: within 2 % of Paper's pixels (text antialiasing aside)");
            Check(ours.w == paper.w && ours.h == paper.h, "render: the board's size (720 x 482 DIP)");
            auto at = [&](const Image& im, float x, float y) { const size_t i = (static_cast<size_t>(std::lround(y * k)) * im.w + static_cast<size_t>(std::lround(x * k))) * 4; return RGB(im.px[i + 2], im.px[i + 1], im.px[i]); };
            auto close = [](COLORREF a, COLORREF b, int t) { return std::abs(GetRValue(a) - GetRValue(b)) <= t && std::abs(GetGValue(a) - GetGValue(b)) <= t && std::abs(GetBValue(a) - GetBValue(b)) <= t; };
            Check(at(ours, 10, 200) == RGB(15, 15, 15) && at(ours, 600, 380) == RGB(23, 23, 23), "preview column #0F0F0F, panel #171717");
            Check(at(ours, 520, 120) == at(paper, 520, 120) && at(ours, 420, 196) == at(paper, 420, 196) && at(ours, 540, 437) == at(paper, 540, 437), "select, chosen segment, Annuler: the board's colours");
            // The accent is the token --color-browser-accent #3E67FF (Paper's PNG export colour-manages it to about #4766F6).
            Check(at(ours, 380, 318) == RGB(0x3E, 0x67, 0xFF) && close(at(paper, 380, 318), RGB(0x3E, 0x67, 0xFF), 12) && at(ours, 650, 437) == RGB(0x3E, 0x67, 0xFF), "accent #3E67FF (box, action)");
        }
        // The state boards: 480 high in Paper (the window is 482: the main board and state 2), so only their top 420 rows
        // are compared when the bottom would be shifted.
        {   // State 1: thumbnails, field focused with « 1-2 ».
            Image paper; Check(paperOf(L"state1-choose.png", paper), "state 1 export loads");
            std::map<int, std::pair<SIZE, std::vector<BYTE>>> images;
            images[0] = {SIZE{120, 170}, Crop(paper, 49, 40, 120, 170).px}; images[1] = {SIZE{120, 170}, Crop(paper, 193, 40, 120, 170).px};
            PD::State s = board; s.chooseRange = true; s.ranges = L"1-2"; D::Render o; o.rangeFocus = true;
            Check(d.RenderForTest(root, outDir + L"\\print-state1-choose.png", 96, s, 3, 0, images, o), "state 1 render");
            compare(L"print-state1-choose", paper, 420, .04, "state 1 (choose by ticking): within 4 % of Paper (page 3's mock content aside)");
        }
        {   // State 2: « 7 » of 3.
            Image paper; Check(paperOf(L"state2-beyond.png", paper), "state 2 export loads");
            std::map<int, std::pair<SIZE, std::vector<BYTE>>> images; images[0] = {SIZE{280, 396}, Crop(paper, 41, 25, 280, 396).px};
            PD::State s = board; s.chooseRange = true; s.ranges = L"7";
            Check(d.RenderForTest(root, outDir + L"\\print-state2-beyond.png", 96, s, 3, 0, images), "state 2 render");
            compare(L"print-state2-beyond", paper, -1, .02, "state 2 (range outside the document): within 2 % of Paper");
        }
        {   // State 3: a printer, landscape.
            Image paper; Check(paperOf(L"state3-printer-landscape.png", paper), "state 3 export loads");
            std::map<int, std::pair<SIZE, std::vector<BYTE>>> images; images[0] = {SIZE{312, 221}, Crop(paper, 25, 112, 312, 221).px};
            PD::State s = board; s.printers = {L"Enregistrer en PDF", L"HP LaserJet Pro M404"}; s.printer = 1; s.landscape = true;
            Check(d.RenderForTest(root, outDir + L"\\print-state3-printer-landscape.png", 96, s, 3, 0, images), "state 3 render");
            compare(L"print-state3-printer-landscape", paper, 420, .03, "state 3 (printer, landscape): within 3 % of Paper");
        }
        {   // State 4: the list without printers.
            Image paper; Check(paperOf(L"state4-no-printer.png", paper), "state 4 export loads");
            std::map<int, std::pair<SIZE, std::vector<BYTE>>> images; images[0] = {SIZE{280, 396}, Crop(paper, 41, 25, 280, 396).px};
            D::Render o; o.listOpen = true;
            Check(d.RenderForTest(root, outDir + L"\\print-state4-no-printer.png", 96, board, 3, 0, images, o), "state 4 render");
            compare(L"print-state4-no-printer", paper, 420, .03, "state 4 (printer list, none installed): within 3 % of Paper");
        }
        {   // State 5: preview being prepared.
            Image paper; Check(paperOf(L"state5-loading.png", paper), "state 5 export loads");
            Check(d.RenderForTest(root, outDir + L"\\print-state5-loading.png", 96, board, -1, 0), "state 5 render");
            compare(L"print-state5-loading", paper, 420, .02, "state 5 (preview being prepared): within 2 % of Paper");
        }
        for (UINT dpi : {96u, 120u}) {   // State 6: preview unavailable.
            const std::wstring suffix = dpi == 96 ? L"" : L"-1.25x";
            Image paper; Check(paperOf((L"state6-unavailable" + suffix + L".png").c_str(), paper), "state 6 export loads");
            D::Render o; o.unavailable = true;
            Check(d.RenderForTest(root, outDir + L"\\print-state6-unavailable" + suffix + L".png", dpi, board, -1, 0, {}, o), "state 6 render");
            compare((L"print-state6-unavailable" + suffix).c_str(), paper, dpi == 96 ? 420 : 525, .03, "state 6 (preview unavailable): within 3 % of Paper");
        }
        {   // State 10: narrow window.
            Image paper; Check(paperOf(L"state10-narrow.png", paper), "state 10 export loads");
            std::map<int, std::pair<SIZE, std::vector<BYTE>>> images; images[0] = {SIZE{32, 44}, Crop(paper, 293, 21, 32, 44).px};   // the board's mini sheet
            D::Render o; o.narrow = true;
            Check(d.RenderForTest(root, outDir + L"\\print-state10-narrow.png", 96, board, 3, 0, images, o), "state 10 render");
            const Image ours = compare(L"print-state10-narrow", paper, -1, .02, "state 10 (narrow window): within 2 % of Paper");
            Check(ours.w == 400 && ours.h == 434, "state 10: 400 x 434");
        }
        {   // State 9: hover and keyboard focus (references for the review; the board is a table, not the window).
            D::Render o; o.hover = D::LandscapeHit; o.focus = D::PortraitHit;
            Check(d.RenderForTest(root, outDir + L"\\print-state9-segment.png", 96, board, 3, 1, {}, o), "state 9: segment hover + focus render");
            o.hover = D::BackgroundsHit; o.focus = D::BackgroundsHit; PD::State s = board; s.backgrounds = false;
            Check(d.RenderForTest(root, outDir + L"\\print-state9-check.png", 96, s, 3, 1, {}, o), "state 9: check row hover + focus render");
            o.hover = D::NextHit; o.focus = D::PrinterHit;
            Check(d.RenderForTest(root, outDir + L"\\print-state9-select-arrow.png", 96, board, 3, 1, {}, o), "state 9: arrow hover + select focus render");
        }
    }

    // 4. Preview worker on generated PDFs.
    if (!PrintPreview::Renderer::Available()) Check(false, "Windows.Data.Pdf available");
    else {
        PrintPreview::Renderer r; Check(r.Start(nullptr, 0), "worker starts");
        auto pdf = std::make_shared<std::vector<BYTE>>(MakePdf(5));
        auto t0 = std::chrono::steady_clock::now(); r.Load(1, pdf); r.Want(1, 3, 210, 297);
        const double callMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        Check(callMs < 20, "Load / Want return at once (work happens on the worker)");
        auto got = WaitFor(r, 1, 10000, 1);
        for (auto& g : got) std::printf("  worker: kind %d generation %u count %d hr 0x%08lX\n", static_cast<int>(g.kind), g.generation, g.count, static_cast<unsigned long>(g.hr));
        Check(!got.empty() && got[0].kind == PrintPreview::Result::Count && got[0].count == 5 && got[0].generation == 1, "5-page PDF: count 5");
        r.Want(1, 3, 210, 297); got = WaitFor(r, 1, 10000, 1);
        Check(!got.empty() && got[0].kind == PrintPreview::Result::Page && got[0].page == 3 && got[0].width == 210 && got[0].height == 297, "page 4 rendered at the size asked");
        if (!got.empty() && got[0].kind == PrintPreview::Result::Page) {
            // Page 4's bar: top 100 + 180 = 280 pt of 842, 40 high, from x 50 to 350 of 595.
            Check(Dark(got[0], 200.f / 595, 300.f / 842) && !Dark(got[0], 200.f / 595, 140.f / 842) && !Dark(got[0], 500.f / 595, 300.f / 842), "page 4: its own bar, nothing where page 1's is");
        }
        // A new document drops the old one's work.
        r.Load(2, std::make_shared<std::vector<BYTE>>(MakePdf(300))); r.Want(1, 0, 100, 141);
        const auto t1 = std::chrono::steady_clock::now(); got = WaitFor(r, 1, 20000, 2);
        Check(!got.empty() && got[0].generation == 2 && got[0].count == 300, "300-page PDF: count 300, nothing from the old document");
        std::printf("  300-page PDF opened in %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
        const auto t2 = std::chrono::steady_clock::now(); r.Want(2, 299, 280, 396); got = WaitFor(r, 1, 10000, 2);
        Check(!got.empty() && got[0].page == 299 && got[0].kind == PrintPreview::Result::Page, "page 300 rendered");
        std::printf("  page 300 drawn in %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count());
        r.Load(3, std::make_shared<std::vector<BYTE>>(std::vector<BYTE>{'n', 'o', 't', ' ', 'p', 'd', 'f'})); got = WaitFor(r, 1, 10000, 3);
        Check(!got.empty() && got[0].kind == PrintPreview::Result::Failed && got[0].generation == 3, "not a PDF: a failure, no crash");
        r.Stop();
    }
    d.Dispose();
    CoUninitialize();   // as the browser at exit: COM unloads Windows.Data.Pdf while the process is alive (its D3D device goes cleanly)
    std::printf("print dialog: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
