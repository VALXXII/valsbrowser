// « Nouveautés » release notes (src/release-notes.h, src/modal.h Spec::notes, Paper p-L-0 142E-0 / 144O-0).
//  A. model: read state format, `previous` syntax, automatic decision (positives and the targeted negatives:
//     first install, dev, ordinary launch / other window, already seen, damaged or future state);
//  B. read state on disk: written only over a missing or own-format file, never over a damaged/future one;
//  C. logo: the real assets/lottie/logo-spin.json through the production ThorVG player: refused by the
//     default limits (other assets keep them), loaded with the opt-in ones, visible ink at several instants
//     and at 20 DIP for 96/144/192 DPI, frames really change, one frame of memory;
//  D. the real modal (offscreen-free window + RenderForTest PNGs): Mini 343 x 218 / Full height to content,
//     Paper geometry, balanced wrapping, focus order, « Voir tout », X / Échap, focus restored, done once,
//     onShown only when really shown, UIA nodes, logo clock only while shown, reduced motion poster,
//     short windows (Full scrolls), FR / EN, 96 / 144 / 192 DPI.
// Usage: release_notes_test.exe <repo root> <output dir>
#include <windows.h>
#include <wincodec.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/modal.h"
#include "../../src/release-notes.h"

using namespace NativeChrome;
namespace RN = ReleaseNotes;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); std::fflush(stdout); }
static void Pump(int ms = 30) { const DWORD end = GetTickCount() + ms; MSG m; while (static_cast<int>(end - GetTickCount()) > 0) while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
static std::string Bytes(const std::wstring& path) { std::ifstream f(std::filesystem::path(path), std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), {}); }
static void Put(const std::wstring& path, const std::string& bytes) { std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc); f << bytes; }

// PNG pixels (BGRA) through WIC.
struct Image { UINT w = 0, h = 0; std::vector<BYTE> px; BYTE At(UINT x, UINT y, int c) const { return px[(static_cast<size_t>(y) * w + x) * 4 + c]; } };
static Image Load(const std::wstring& png) {
    Image img; IWICImagingFactory* f = nullptr; IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && SUCCEEDED(f->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &d))
        && SUCCEEDED(d->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv)) && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
        && SUCCEEDED(cv->GetSize(&img.w, &img.h))) { img.px.resize(static_cast<size_t>(img.w) * img.h * 4); if (FAILED(cv->CopyPixels(nullptr, img.w * 4, static_cast<UINT>(img.px.size()), img.px.data()))) img.px.clear(); }
    if (cv) cv->Release(); if (fr) fr->Release(); if (d) d->Release(); if (f) f->Release();
    return img;
}
// Pixels in a DIP rectangle brighter than the #171717 surface by more than `delta`; bounding box returned.
struct Ink { int count = 0; float l = 1e9f, t = 1e9f, r = -1, b = -1; int peak = 0; };
static Ink InkIn(const Image& img, float scale, D2D1_RECT_F dip, int delta = 12) {
    Ink k; if (img.px.empty()) return k;
    for (UINT y = static_cast<UINT>(std::max(0.f, dip.top * scale)); y < std::min<float>(img.h, dip.bottom * scale); ++y)
        for (UINT x = static_cast<UINT>(std::max(0.f, dip.left * scale)); x < std::min<float>(img.w, dip.right * scale); ++x) {
            const int v = img.At(x, y, 1); if (v - 0x17 > delta) { ++k.count; k.peak = std::max(k.peak, v); k.l = std::min(k.l, x / scale); k.t = std::min(k.t, y / scale); k.r = std::max(k.r, (x + 1) / scale); k.b = std::max(k.b, (y + 1) / scale); }
        }
    return k;
}
static Modal::Spec NotesSpec(const std::wstring& root, bool mini) {
    std::vector<Modal::NoteSection> sections; const auto notes = RN::Current();
    for (const auto& s : notes) sections.push_back({s.kind == RN::Kind::Fixed, s.title, s.items});
    return Modal::ReleaseNotesSpec(std::move(sections), RN::Spoken(notes), root + L"\\assets\\lottie\\logo-spin.json", mini);
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&cc);
    const std::wstring root = argc > 1 ? argv[1] : L".", out = argc > 2 ? argv[2] : L"build/release-notes";
    std::filesystem::create_directories(std::filesystem::path(out));
    const std::wstring logoPath = root + L"\\assets\\lottie\\logo-spin.json";

    // ---- A. model ------------------------------------------------------------------------------------
    {
        Check(RN::Parse("").status == RN::Status::Missing, "state: empty = missing");
        const auto ok = RN::Parse(RN::Format("abc-1"));
        Check(ok.status == RN::Status::Ok && ok.seen == "abc-1", "state: own format round trip");
        Check(RN::Parse("\xEF\xBB\xBF" "browser-release-notes 1\r\nseen=abc-1\r\n").status == RN::Status::Ok, "state: BOM and CRLF accepted");
        Check(RN::Parse("browser-release-notes 2\nseen=abc\n").status == RN::Status::UnknownVersion, "state: future format recognised");
        Check(RN::Parse("garbage").status == RN::Status::Invalid, "state: garbage = invalid");
        Check(RN::Parse("browser-release-notes 1\nseen=a\nseen=b\n").status == RN::Status::Invalid, "state: two seen lines = invalid");
        Check(RN::Parse("browser-release-notes 1\nseen=Bad Id\n").status == RN::Status::Invalid, "state: invalid id = invalid");
        Check(RN::Parse("browser-release-notes 1\n").status == RN::Status::Invalid, "state: header only = invalid");
        Check(RN::ValidId(RN::NotesId), "NotesId is a valid id");

        using PK = RN::Previous::Kind;
        Check(RN::ParsePrevious("").kind == PK::None, "previous: absent = none (first install)");
        { const auto p = RN::ParsePrevious("installed 0.3.379\n"); Check(p.kind == PK::Installed && p.version == "0.3.379", "previous: installed M.m.B"); }
        Check(RN::ParsePrevious("installed 0.3.379\r\n").kind == PK::Installed, "previous: CRLF");
        Check(RN::ParsePrevious("portable C:\\Users\\x\\browser\n").kind == PK::Portable, "previous: portable path");
        for (const char* bad : {"installed 0.3\n", "installed 0.3.x\n", "installed 0..3\n", "installed 0.3.1.2\n", "installed  0.3.1\n", "portable \n", "repair 0.3.1\n", "installed 0.3.1\nportable x\n", "installed 0.3.1234567\n"})
            Check(RN::ParsePrevious(bad).kind == PK::Invalid, (std::string("previous: invalid syntax refused: ") + bad).c_str());

        const RN::State missing{}, seenOther{"older-notes", RN::Status::Ok}, seenNow{RN::NotesId, RN::Status::Ok};
        RN::Previous inst; inst.kind = PK::Installed; inst.version = "0.3.379";
        RN::Previous same = inst; same.version = "0.3.380";
        RN::Previous port; port.kind = PK::Portable;
        RN::Previous none, invalid; invalid.kind = PK::Invalid;
        using D = RN::Decision;
        Check(RN::Decide(true, inst, "0.3.380", missing) == D::Show, "decide: update from an installed version, never seen -> Mini");
        Check(RN::Decide(true, inst, "0.3.380", seenOther) == D::Show, "decide: update, other notes seen -> Mini");
        Check(RN::Decide(true, port, "0.3.380", missing) == D::Show, "decide: update from a portable copy -> Mini");
        Check(RN::Decide(true, inst, "0.3.380", seenNow) == D::Hidden, "decide NEGATIVE: these notes already seen");
        Check(RN::Decide(false, inst, "0.3.380", missing) == D::Hidden, "decide NEGATIVE: ordinary launch / another window (no Installed at start)");
        Check(RN::Decide(true, inst, "dev", missing) == D::Hidden, "decide NEGATIVE: dev build (no M.m.B version)");
        Check(RN::Decide(true, none, "0.3.380", missing) == D::Baseline, "decide NEGATIVE: first install -> baseline only, no modal");
        Check(RN::Decide(true, none, "0.3.380", seenOther) == D::Hidden, "decide: no previous, state present -> nothing");
        Check(RN::Decide(true, same, "0.3.380", missing) == D::Hidden, "decide NEGATIVE: previous = this version (repair)");
        Check(RN::Decide(true, invalid, "0.3.380", missing) == D::Hidden, "decide NEGATIVE: unreadable/invalid previous");
        Check(RN::Decide(true, inst, "0.3.380", RN::State{"", RN::Status::UnknownVersion}) == D::Hidden, "decide NEGATIVE: future state format");
        Check(RN::Decide(true, inst, "0.3.380", RN::State{"", RN::Status::Invalid}) == D::Hidden, "decide NEGATIVE: damaged state");
        Check(RN::Decide(true, inst, "0.3.380", RN::State{"", RN::Status::Unreadable}) == D::Hidden, "decide NEGATIVE: unreadable state");

        // Content: the Paper text (FR), the same structure in EN.
        I18n::SetForProcess(I18n::Locale::Fr);
        auto fr = RN::Current();
        Check(fr.size() == 2 && fr[0].kind == RN::Kind::Added && fr[0].items.size() == 2 && fr[1].kind == RN::Kind::Fixed && fr[1].items.size() == 3, "content: Ajouts 2 + Corrections 3");
        Check(fr[0].title == L"Ajouts" && fr[1].title == L"Corrections" && fr[0].items[0] == L"Français ou anglais, au choix dans réglages." && fr[0].items[1] == L"Sélection de l’adresse aux couleurs du profil."
              && fr[1].items[0] == L"Changement d’onglet sans flou." && fr[1].items[1] == L"Le plein écran masque la barre du navigateur." && fr[1].items[2] == L"Renommage des dossiers de favoris sur place.", "content FR: Paper text verbatim");
        Check(std::wstring(TR(release_notes_title)) == L"Nouveautés" && std::wstring(TR(release_notes_show_all)) == L"Voir tout", "content FR: title and Voir tout");
        I18n::SetForProcess(I18n::Locale::En);
        auto en = RN::Current();
        Check(en.size() == 2 && en[0].items.size() == 2 && en[1].items.size() == 3 && en[0].title == L"Added" && en[1].title == L"Fixed" && std::wstring(TR(release_notes_show_all)) == L"Show all", "content EN: same structure, English text");
        bool differ = true; for (size_t s = 0; s < 2; ++s) for (size_t i = 0; i < en[s].items.size(); ++i) differ = differ && en[s].items[i] != fr[s].items[i];
        Check(differ, "content EN: every item translated");
        const auto spoken = RN::Spoken(en);
        Check(spoken.find(L"Added.") != std::wstring::npos && spoken.find(L"Bookmark folders renamed in place.") != std::wstring::npos, "spoken: all sections and items");
        I18n::SetForProcess(I18n::Locale::Fr);
    }

    // ---- B. read state on disk ----------------------------------------------------------------------------
    {
        const std::wstring dir = out + L"\\state"; std::filesystem::remove_all(std::filesystem::path(dir)); std::filesystem::create_directories(std::filesystem::path(dir));
        const std::wstring file = RN::PathIn(dir);
        Check(RN::Load(dir).status == RN::Status::Missing, "disk: missing file");
        Check(RN::MarkSeen(dir, RN::NotesId) && RN::Load(dir).seen == RN::NotesId && Bytes(file) == RN::Format(RN::NotesId), "disk: MarkSeen writes the own format");
        Check(RN::MarkSeen(dir, "next-notes") && RN::Load(dir).seen == "next-notes", "disk: a newer content id replaces the old one");
        bool noTemp = true; for (auto& e : std::filesystem::directory_iterator(std::filesystem::path(dir))) noTemp = noTemp && e.path().filename().wstring().find(L".tmp") == std::wstring::npos;
        Check(noTemp, "disk: no temp file left");
        const std::string future = "browser-release-notes 7\nseen=x\nwhatever\n"; Put(file, future);
        Check(!RN::MarkSeen(dir, RN::NotesId) && Bytes(file) == future, "disk: a future format is never overwritten");
        const std::string damaged = "\x01\x02 not ours"; Put(file, damaged);
        Check(!RN::MarkSeen(dir, RN::NotesId) && Bytes(file) == damaged, "disk: a damaged file is never overwritten");
        std::filesystem::remove(std::filesystem::path(file)); std::filesystem::create_directories(std::filesystem::path(file));   // a folder where the file should be
        Check(RN::Load(dir).status == RN::Status::Unreadable && !RN::MarkSeen(dir, RN::NotesId), "disk: unreadable target -> nothing written");
        Check(RN::Load(L"").status == RN::Status::Unreadable && !RN::MarkSeen(L"", RN::NotesId), "disk: no data root -> nothing written");
        Check(!RN::MarkSeen(dir + L"\\missing-folder", RN::NotesId) && !std::filesystem::exists(std::filesystem::path(dir + L"\\missing-folder")), "disk: write failure reported, no folder created");
    }

    // ---- C. logo through the production ThorVG player --------------------------------------------------
    {
        Check(std::filesystem::file_size(std::filesystem::path(logoPath)) == 1143955, "logo: repository copy has the source size (byte identity checked by SHA-256 in the runner)");
        Lottie::Asset defaults; Check(!defaults.Load(logoPath), "logo: refused by the DEFAULT limits (256 KiB / 120 frames still protect other assets)");
        for (const auto* name : {L"refresh", L"copy-link", L"audio-meter", L"download-arrow"}) { Lottie::Asset a; Check(a.Load(root + L"\\assets\\lottie\\" + name + L".json") && a.CacheBytes() == 0, "logo: existing assets still load with the defaults"); }
        Lottie::Limits tight; tight.maxBytes = 2u * 1024 * 1024; tight.maxFrames = 200;
        Lottie::Asset few; Check(!few.Load(logoPath, tight), "logo: a frame ceiling below 255 still refuses it");
        Lottie::Limits lim; lim.maxBytes = 2u * 1024 * 1024; lim.maxFrames = 256; lim.cacheFrames = false;
        Lottie::Asset logo; Check(logo.Load(logoPath, lim), "logo: loads with the opt-in limits");
        Check(logo.FrameCount() == 255 && std::fabs(logo.Duration() - 4.25) < 1e-3, "logo: 255 frames, 4.25 s (ip 0 / op 255 at 60 fps)");
        Check(!logo.Sample(0, 65) && !logo.Sample(0, 0), "logo: sizes outside 1..64 px refused");
        for (unsigned side : {20u, 30u, 40u}) {
            std::vector<uint32_t> first; int changed = 0;
            for (double p : {0.0, .125, .25, .5, .75, .999}) {
                const auto* f = logo.Sample(p, side);
                if (!f || f->bgra.size() != side * side) { Check(false, "logo: frame rendered"); continue; }
                int ink = 0, l = 999, r = -1, t = 999, b = -1;
                for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) if ((f->bgra[y * side + x] >> 24) > 32) { ++ink; l = std::min<int>(l, x); r = std::max<int>(r, x); t = std::min<int>(t, y); b = std::max<int>(b, y); }
                char what[160]; std::snprintf(what, sizeof what, "logo: %u px, progress %.3f: visible ink %d px, box %dx%d inside the slot", side, p, ink, r - l + 1, b - t + 1);
                Check(ink > static_cast<int>(side * side) / 12 && l >= 0 && r < static_cast<int>(side) && (r - l + 1) >= static_cast<int>(side) / 4, what);
                if (first.empty()) first = f->bgra; else if (f->bgra != first) ++changed;
            }
            Check(changed >= 3, "logo: frames really change along the turn");
            Check(logo.CacheBytes() <= static_cast<size_t>(side) * side * 4 * 2, "logo: memory stays one frame (+ scratch), never 255 cached frames");
        }
    }

    // ---- D. the real modal ---------------------------------------------------------------------------
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"NotesTestOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, 100, 100, 1000, 700, nullptr, nullptr, hi, nullptr);
    HWND ownerEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 10, 10, 200, 24, owner, nullptr, hi, nullptr);
    using D = Modal::Dialog;
    Modal::Dialog d; Check(d.Create(hi, owner, root, {}), "Dialog::Create");
    d.SetReducedMotion(false);
    const RECT area{100, 100, 1100, 800};
    for (I18n::Locale lang : {I18n::Locale::Fr, I18n::Locale::En}) {
        I18n::SetForProcess(lang); const std::wstring tag = lang == I18n::Locale::Fr ? L"fr" : L"en"; const bool fr = lang == I18n::Locale::Fr;
        int calls = 0, button = -9, shown = 0;
        SetFocus(ownerEdit);
        auto spec = NotesSpec(root, true); spec.onShown = [&] { ++shown; };
        Check(d.Show(spec, area, [&](const Modal::Result& r) { ++calls; button = r.button; }), "mini: Show");
        Check(shown == 1, "mini: onShown once, when really shown");
        { auto again = NotesSpec(root, true); int shown2 = 0; again.onShown = [&] { ++shown2; }; Check(!d.Show(again, area, [](const Modal::Result&) {}) && shown2 == 0, "mini: a refused Show never reports shown"); }
        d.FinishAnimationsForTest(); Pump(40);
        Check(std::fabs(d.WidthForTest() - 343) < .01f && std::fabs(d.HeightForTest() - 218) < .01f, "mini: 343 x 218 DIP (Paper 142E-0)");
        Check(d.FocusForTest() == D::NotesExpandHit, "mini: initial focus on Voir tout");
        const auto btn = d.BoxForTest(D::NotesExpandHit), close = d.BoxForTest(D::CloseHit);
        Check(std::fabs(btn.top - 170) < .01f && std::fabs(btn.bottom - 202) < .01f && std::fabs(btn.left - 17) < .01f && std::fabs(btn.right - 326) < .01f, "mini: Voir tout 309 x 32 at y 170 (footer padding 16)");
        Check(std::fabs(close.left - 307.6f) < .01f && std::fabs(close.top - 11.8f) < .01f && std::fabs(close.right - close.left - 24) < .01f, "mini/full: close 24 at top 10.8 / right 10.4 in the padding box");
        const auto blocks = d.NoteBlocksForTest();   // rule, heading, item, item, rule, heading, item, item, item
        Check(blocks.size() == 9 && std::fabs(blocks[0].top - 53) < .01f && std::fabs(blocks[1].top - 70) < .01f && std::fabs(blocks[2].top - 106) < .01f, "layout: rule at 53, heading at 70, first item at 106 (16 gaps, 20 rows)");
        if (fr && blocks.size() == 9) {
            const float h[] = {blocks[2].bottom - blocks[2].top, blocks[3].bottom - blocks[3].top, blocks[6].bottom - blocks[6].top, blocks[7].bottom - blocks[7].top, blocks[8].bottom - blocks[8].top};
            char what[200]; std::snprintf(what, sizeof what, "layout FR: Paper wrapping 2/2 then 1/2/2 lines (got %.0f %.0f / %.0f %.0f %.0f)", h[0], h[1], h[2], h[3], h[4]);
            Check(h[0] == 40 && h[1] == 40 && h[2] == 20 && h[3] == 40 && h[4] == 40, what);
        }
        auto nodes = d.NodesForTest(); int texts = 0, hidden = 0; bool group = false, showAll = false, closeNode = false;
        for (auto& n : nodes) { if (n.role == Accessibility::Role::Text) { ++texts; if (n.offscreen) ++hidden; } if (n.key == 1 && n.name == (fr ? L"Nouveautés" : L"What’s new") && n.help.find(fr ? L"Renommage des dossiers de favoris sur place." : L"Bookmark folders renamed in place.") != std::wstring::npos) group = true; if (n.key == D::NotesExpandHit && n.automationId == L"ReleaseNotesShowAll") showAll = true; if (n.key == D::CloseHit && n.automationId == L"DialogClose") closeNode = true; }
        Check(group && showAll && closeNode && texts == 7, "uia mini: named group with every item in its description, 7 text nodes, Voir tout, Fermer");
        Check(hidden >= 3, "uia mini: items under the fade are offscreen (not claimed visible)");
        Check(d.LogoAnimatingForTest(), "logo: the clock runs while the dialog is shown");
        Check(d.NextWake(Motion::Now()).frames, "logo: frames requested while shown");
        for (UINT dpi : {96u, 144u, 192u}) {
            const float scale = dpi / 96.f; int i = 0;
            for (double at : {0.0, 1.0, 2.1, 3.3}) {
                d.LogoAtForTest(at); const std::wstring png = out + L"\\mini-" + tag + L"-" + std::to_wstring(dpi) + (at == 0 ? L"" : L"-t" + std::to_wstring(i)) + L".png"; ++i;
                Check(d.RenderForTest(png, dpi), "render mini");
                const auto img = Load(png); const auto ink = InkIn(img, scale, {17, 17, 37, 37});
                char what[200]; std::snprintf(what, sizeof what, "pixels %ls %u dpi t=%.1fs: %ux%u px, logo ink %d px (box %.1f..%.1f x %.1f..%.1f DIP)", tag.c_str(), dpi, at, img.w, img.h, ink.count, ink.l, ink.r, ink.t, ink.b);
                Check(std::abs(static_cast<int>(img.w) - 343 * scale) <= 1 && std::abs(static_cast<int>(img.h) - 218 * scale) <= 1 && ink.count > static_cast<int>(20 * scale * 20 * scale / 12) && ink.l >= 17 && ink.r <= 37 && ink.peak >= 0x60 && ink.peak <= 0x9A, what);
            }
            const auto img = Load(out + L"\\mini-" + tag + L"-" + std::to_wstring(dpi) + L".png");
            Check(InkIn(img, scale, {43, 17, 160, 37}).count > 50, "pixels: title drawn right of the logo (x 43)");
            Check(InkIn(img, scale, {20, 172, 323, 200}, 8).count > 20 && img.At(static_cast<UINT>(30 * scale), static_cast<UINT>(186 * scale), 1) == 0x29, "pixels: Voir tout raised #292929 with its label");
            Check(InkIn(img, scale, {17, 205, 326, 216}).count == 0, "pixels: the fade hides the content under the button");
            Check(InkIn(img, scale, {43, 106, 326, 125}).count > 50, "pixels: first item visible above the fade");
        }
        // Keyboard: Tab cycles Voir tout <-> Fermer; Entrée on Voir tout expands in place.
        d.KeyForTest(VK_TAB); Check(d.FocusForTest() == D::CloseHit, "keyboard: Tab -> Fermer");
        d.KeyForTest(VK_TAB); Check(d.FocusForTest() == D::NotesExpandHit, "keyboard: Tab wraps to Voir tout");
        d.KeyForTest(VK_RETURN);
        Check(calls == 0 && d.Visible() && !d.NotesCollapsedForTest(), "Voir tout: same dialog, Full view, nothing answered");
        Check(d.FocusForTest() == D::CloseHit, "Voir tout: focus moves to Fermer (the button is gone)");
        const float full = d.HeightForTest();
        { char what[120]; std::snprintf(what, sizeof what, "full: height to content %.0f DIP (Paper 144O-0: about 390 in FR)", full); Check(full > 300 && (!fr || std::fabs(full - 390) < .5f) && d.ScrollMaxForTest() == 0, what); }
        nodes = d.NodesForTest(); texts = hidden = 0; bool noShowAll = true;
        for (auto& n : nodes) { if (n.role == Accessibility::Role::Text) { ++texts; if (n.offscreen) ++hidden; } if (n.key == D::NotesExpandHit) noShowAll = false; }
        Check(texts == 7 && hidden == 0 && noShowAll, "uia full: every item on screen, no Voir tout");
        for (UINT dpi : {96u, 144u, 192u}) {
            const float scale = dpi / 96.f; const std::wstring png = out + L"\\full-" + tag + L"-" + std::to_wstring(dpi) + L".png";
            Check(d.RenderForTest(png, dpi), "render full");
            const auto img = Load(png);
            Check(std::abs(static_cast<int>(img.h) - full * scale) <= 1 && InkIn(img, scale, {43, full - 17 - 20, 326, full - 17}).count > 30, "pixels full: last item drawn at the bottom padding");
        }
        d.KeyForTest(VK_ESCAPE);
        Check(calls == 1 && button == -1 && !d.Visible(), "Échap closes once (-1)");
        Check(GetFocus() == ownerEdit, "focus restored to the owner's control");
        d.FinishAnimationsForTest();
        Check(!d.LogoAnimatingForTest() && !d.NextWake(Motion::Now()).frames && !d.NextWake(Motion::Now()).delay && !d.LogoLoadedForTest() && d.LogoBytesForTest() == 0, "closed: no clock, player and pixels released");
    }
    I18n::SetForProcess(I18n::Locale::Fr);
    {   // X closes; Full view (manual reopening) starts on Fermer; done once.
        int calls = 0, button = -9;
        d.Show(NotesSpec(root, false), area, [&](const Modal::Result& r) { ++calls; button = r.button; }); d.FinishAnimationsForTest();
        Check(d.FocusForTest() == D::CloseHit && !d.NotesCollapsedForTest(), "full (À propos): opens on Fermer");
        d.ActivateForTest(D::CloseHit); d.ActivateForTest(D::CloseHit);
        Check(calls == 1 && button == -1, "X closes once (-1)");
        d.FinishAnimationsForTest();
    }
    {   // Bottom-right card (user request 2026-11-16): 14 DIP from the owner client's right and bottom edges, no scrim,
        // and the automatic card (activate = false) leaves the keyboard where it was; it is not light-dismissed.
        SetFocus(ownerEdit); int calls = 0;
        auto spec = NotesSpec(root, true); spec.activate = false;
        Check(spec.scope == Modal::Scope::Corner, "card: Scope::Corner");
        d.Show(spec, area, [&](const Modal::Result&) { ++calls; }); d.FinishAnimationsForTest();
        RECT client{}; GetClientRect(owner, &client); MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&client), 2);
        RECT card{}; GetWindowRect(d.Window(), &card); const float s = GetDpiForWindow(owner) / 96.f;
        char what[200]; std::snprintf(what, sizeof what, "card: bottom-right, %ld / %ld px from the client's right / bottom edges (14 DIP = %.0f px)", client.right - card.right, client.bottom - card.bottom, 14 * s);
        Check(std::abs((client.right - card.right) - 14 * s) <= 1 && std::abs((client.bottom - card.bottom) - 14 * s) <= 1, what);
        Check(!IsWindowVisible(d.Scrim()) && !d.BlocksWindow(), "card: no scrim, the window stays usable");
        Check(GetFocus() == ownerEdit && GetForegroundWindow() != d.Window(), "card (automatic): the keyboard stays where it was");
        SetFocus(ownerEdit); Pump(60);
        Check(d.Visible(), "card: not dismissed when the owner is used");
        d.ActivateForTest(D::NotesExpandHit); Check(!d.NotesCollapsedForTest() && d.Visible(), "card: Voir tout (click) expands in place");
        RECT full{}; GetWindowRect(d.Window(), &full);
        Check(full.bottom == card.bottom && full.right == card.right && full.top < card.top, "card: the Full view grows upwards, same corner");
        d.ActivateForTest(D::CloseHit);
        Check(calls == 1 && GetFocus() == ownerEdit, "card: X closes once and leaves the focus where it was");
        d.FinishAnimationsForTest();
    }
    {   // Reduced motion: no clock, frame 0 poster really visible.
        d.SetReducedMotion(true); int calls = 0;
        d.Show(NotesSpec(root, true), area, [&](const Modal::Result&) { ++calls; }); d.FinishAnimationsForTest();
        Check(!d.LogoAnimatingForTest() && !d.NextWake(Motion::Now()).frames, "reduced motion: no logo frames");
        for (UINT dpi : {96u, 144u, 192u}) {
            const std::wstring png = out + L"\\mini-reduced-" + std::to_wstring(dpi) + L".png"; d.RenderForTest(png, dpi);
            const auto ink = InkIn(Load(png), dpi / 96.f, {17, 17, 37, 37});
            Check(d.LogoLoadedForTest() && ink.count > static_cast<int>(dpi * dpi / 96.f / 96.f * 400 / 12), "reduced motion: poster (frame 0) visible in the slot");
        }
        d.Abort(); d.FinishAnimationsForTest(); d.SetReducedMotion(false);
        Check(calls == 1, "Abort answers once");
    }
    {   // Short window: the Full view is capped and its body scrolls; nothing becomes unreachable.
        int calls = 0; const RECT small{100, 100, 700, 100 + 260};
        SetWindowPos(owner, nullptr, 100, 100, 600, 260, SWP_NOZORDER | SWP_NOACTIVATE);   // browser dialogs fit the owner's client area
        d.Show(NotesSpec(root, false), small, [&](const Modal::Result&) { ++calls; }); d.FinishAnimationsForTest();
        const float h = d.HeightForTest(), max = d.ScrollMaxForTest();
        { char what[120]; std::snprintf(what, sizeof what, "short window: full height capped (%.0f) and scrollable (%.0f DIP)", h, max); Check(h <= 260 - 32 + .5f && max > 0, what); }
        for (int k = 0; k < 40 && d.ScrollForTest() < max - .5f; ++k) d.KeyForTest(VK_NEXT);
        Check(std::fabs(d.ScrollForTest() - max) < .5f, "short window: Pg↓ reaches the last item");
        auto nodes = d.NodesForTest(); bool lastOn = false; for (auto& n : nodes) if (n.role == Accessibility::Role::Text && n.name == L"Renommage des dossiers de favoris sur place.") lastOn = !n.offscreen;
        Check(lastOn, "short window: last item on screen after scrolling (UIA)");
        Check(d.RenderForTest(out + L"\\full-short-scrolled-144.png", 144), "render short scrolled");
        for (int k = 0; k < 40 && d.ScrollForTest() > 0; ++k) d.KeyForTest(VK_PRIOR);
        Check(d.ScrollForTest() == 0, "short window: Pg↑ back to the top");
        d.KeyForTest(VK_ESCAPE); Check(calls == 1, "short window: Échap closes");
        d.FinishAnimationsForTest();
        // Mini in a very short window: smaller than 218, Voir tout stays on screen.
        const RECT tiny{100, 100, 700, 100 + 200};
        SetWindowPos(owner, nullptr, 100, 100, 600, 200, SWP_NOZORDER | SWP_NOACTIVATE);
        d.Show(NotesSpec(root, true), tiny, [](const Modal::Result&) {}); d.FinishAnimationsForTest();
        const auto b = d.BoxForTest(D::NotesExpandHit);
        Check(d.HeightForTest() < 218 && b.bottom <= d.HeightForTest() - 16 + .01f && b.top > 0, "tiny window: Mini shrinks, Voir tout still inside");
        d.Abort(); d.FinishAnimationsForTest();
        SetWindowPos(owner, nullptr, 100, 100, 1000, 700, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    {   // Missing logo: the Paper still glyph is drawn instead of an empty slot.
        auto spec = NotesSpec(root, true); spec.notes.logo = root + L"\\assets\\lottie\\missing.json";
        d.Show(spec, area, [](const Modal::Result&) {}); d.FinishAnimationsForTest();
        const std::wstring png = out + L"\\mini-no-lottie-144.png"; d.RenderForTest(png, 144);
        Check(!d.LogoLoadedForTest() && !d.LogoAnimatingForTest() && InkIn(Load(png), 1.5f, {17, 17, 37, 37}).count > 40, "missing Lottie: static Paper glyph, no clock");
        d.Abort(); d.FinishAnimationsForTest();
    }
    d.Dispose(); DestroyWindow(owner); CoUninitialize();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
