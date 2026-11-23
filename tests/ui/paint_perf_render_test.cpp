// Paint-path performance (audit PERF-03, PERF-04, PERF-06, PERF-09): offscreen Direct2D only, no window,
// no WebView2. Saves captures of the bookmarks bar, the audio meter and the downloads icon (compared
// byte for byte against the same fixture built on the previous sources: pixels must not change) and
// measures the work: bitmap uploads per paint, text layouts per paint, and the frame-clock wakes and
// icon repaints a second that the host's scheduling rule (main.cpp ScheduleMotion) gives each animation.
//
// Usage: paint_perf_render_test.exe <repo-root> <out-dir> [--baseline]
//   --baseline: measure and capture only (the previous sources fail the new expectations by design).
#include "../../src/chrome.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace NativeChrome;
static bool baseline = false;
static int failures = 0;
static void Expect(bool ok, const char* what) { if (baseline) return; std::printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++failures; }
static Motion::Time T(long long ms) { return Motion::Time{} + std::chrono::seconds(100) + std::chrono::milliseconds(ms); }
// Counter added with the fix; the previous sources (without the instrumentation) report -1.
template<class C> auto Uploads(const C& c, int) -> decltype(static_cast<long long>(c.faviconUploads)) { return static_cast<long long>(c.faviconUploads); }
template<class C> long long Uploads(const C&, long) { return -1; }

static std::shared_ptr<const FaviconImage> Image(unsigned side, unsigned seed) {
    auto img = std::make_shared<FaviconImage>(); img->width = img->height = side; img->stride = side * 4; img->pixels.resize(img->stride * side);
    for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
        unsigned char* p = &img->pixels[y * img->stride + x * 4];
        p[0] = static_cast<unsigned char>((x * 16 + seed * 40) & 255); p[1] = static_cast<unsigned char>((y * 16 + seed * 90) & 255); p[2] = static_cast<unsigned char>((seed * 70) & 255); p[3] = 255;
    }
    return img;
}

// ---- Host scheduling, as main.cpp ScheduleMotion: frame wakes (or a deadline within one nominal frame)
// are served by the next compositor tick, longer deadlines by a one-shot timer.
struct Load { long long wakes = 0, ticks = 0, timers = 0, legacyRepaints = 0, poseRepaints = 0; double seconds = 0; };
template<class Settle, class Wake, class Same>
static Load Simulate(Settle settle, Wake wake, Same same, Motion::Time start, int ms, double hz) {
    Load load; load.seconds = ms / 1000.0;
    const double period = 1000.0 / hz; double t = 0; bool painting = false, known = false;
    auto at = [&](double v) { return start + std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double, std::milli>(v)); };
    while (t < ms) {
        const auto now = at(t); settle(now);
        // Previous invalidation rule: every tick while the icon moves, plus the one after; new rule: on a pose change.
        const auto w = wake(now); const bool moving = w.delay.has_value();
        if (moving || painting) ++load.legacyRepaints; painting = moving;
        if (!known || !same(now)) ++load.poseRepaints; known = true;
        if (!w.delay) break;
        ++load.wakes;
        if (w.frames || *w.delay <= *Motion::Frame().delay) { ++load.ticks; t = (std::floor(t / period + 1e-9) + 1) * period; }
        else { ++load.timers; t += static_cast<double>(w.delay->count()); }
    }
    return load;
}
static void Print(const char* what, double hz, const Load& l) {
    std::printf("  %-26s %3.0f Hz: wakes %6.1f/s (ticks %6.1f, timers %5.1f)  repaints %6.1f/s (every-tick rule %6.1f/s)\n", what, hz,
                l.wakes / l.seconds, l.ticks / l.seconds, l.timers / l.seconds, l.poseRepaints / l.seconds, l.legacyRepaints / l.seconds);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring root = argv[1], out = argv[2];
    baseline = argc > 3 && std::wstring(argv[3]) == L"--baseline";
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    auto png = [&](const std::wstring& name) { return out + L"\\" + name + L".png"; };

    // ---- PERF-04 / PERF-06: bookmarks bar ------------------------------------------------------------
    for (int count : {12, 150}) {
        Chrome c; if (FAILED(c.InitOffscreen(root, 96, 1180, 121))) return 4;
        Motion::State m(false); c.SetMotion(&m);
        std::vector<std::shared_ptr<const FaviconImage>> tabIcons{Image(16, 1), Image(32, 2)};
        c.bandShown = true;
        for (int i = 0; i < count; ++i) { Chrome::BandItem item; item.id = 100 + i; item.title = L"Signet " + std::to_wstring(i) + (i % 3 ? L" exemple" : L""); item.tip = item.title;
            item.folder = i == 3; item.image = item.folder ? nullptr : Image(i % 2 ? 16 : 64, 10 + i); c.bandItems.push_back(item); }
        c.address = AddressBar::Describe(L"https://exemple.test/page");
        const std::vector<std::wstring> titles{L"Exemple", L"Wikipédia"};
        int barImages = 0; for (int i = 0; i < count && i < 12; ++i) if (c.bandItems[i].image) ++barImages;
        long long layouts[4]{}, uploads[4]{};
        for (int p = 0; p < 4; ++p) {
            const auto beforeLayouts = c.textLayoutsCreated; const long long beforeUploads = Uploads(c, 0);
            c.SetFavicons(tabIcons);   // as WM_PAINT does before every paint
            c.Paint(titles, 0, L"", false, 0, 0, nullptr, T(0));
            layouts[p] = static_cast<long long>(c.textLayoutsCreated - beforeLayouts); uploads[p] = beforeUploads < 0 ? -1 : Uploads(c, 0) - beforeUploads;
        }
        std::printf("bookmarks bar, %d items (%d visible with an image): text layouts per paint %lld, %lld, %lld, %lld; bitmap uploads per paint %lld, %lld, %lld, %lld\n",
                    count, barImages, layouts[0], layouts[1], layouts[2], layouts[3], uploads[0], uploads[1], uploads[2], uploads[3]);
        c.SavePng(png(L"bar-" + std::to_wstring(count)).c_str());
        if (count == 12) {
            Expect(uploads[0] >= 2 + barImages - 1 && uploads[1] == 0 && uploads[2] == 0 && uploads[3] == 0, "PERF-04: bar favicons uploaded once, not on every paint");
            // A new image for one bookmark: that one alone is uploaded; the replaced one leaves the cache.
            c.bandItems[0].image = Image(16, 99); const long long before = Uploads(c, 0);
            c.SetFavicons(tabIcons); c.Paint(titles, 0, L"", false, 0, 0, nullptr, T(0));
            Expect(Uploads(c, 0) - before == 1, "PERF-04: a changed bar image is uploaded again (only that one)");
            c.SavePng(png(L"bar-12-changed").c_str());
        } else {
            Expect(layouts[1] == 0 && layouts[2] == 0 && layouts[3] == 0, "PERF-06: 150 bar items: no text layout rebuilt on later paints (cache not emptied mid-paint)");
        }
    }

    // ---- PERF-03: audio meter ------------------------------------------------------------------------
    {
        Chrome c; if (FAILED(c.InitOffscreen(root, 96, 1180, 84))) return 5;
        Motion::State m(false); c.SetMotion(&m);
        c.audioShown = true; c.audioIcon.Show(true, T(-1000)); c.audioIcon.Update(true, T(0), false);
        const std::vector<std::wstring> titles{L"Musique"};
        for (long long ms : {0LL, 100LL, 250LL, 400LL, 469LL, 1000LL, 2600LL, 4100LL}) {
            c.audioIcon.Settle(T(ms)); c.Paint(titles, 0, L"", false, 0, 0, nullptr, T(ms));
            c.SavePng(png(L"audio-" + std::to_wstring(ms)).c_str());
        }
        // A steady loop: 5 s of music, from 1 s in (after the intro).
        for (double hz : {60.0, 144.0, 240.0}) {
            Audio::IconModel model; model.Show(true, T(-1000)); model.Update(true, T(0), false); model.Settle(T(1000));
            Audio::IconPose last{}; bool seen = false;
            auto l = Simulate([&](Motion::Time now) { model.Settle(now); }, [&](Motion::Time now) { return model.NextWake(now); },
                [&](Motion::Time now) { const auto p = model.At(now); const bool same = seen && p.frame == last.frame && p.high == last.high && p.emphasis == last.emphasis && p.presence == last.presence; last = p; seen = true; return same; },
                T(1000), 5000, hz);
            Print("audio meter (music loop)", hz, l);
            Expect(l.poseRepaints <= 15 * 5 + 1, "PERF-03: about 14 meter repaints a second (one per loop step) whatever the display rate");
            Expect(l.wakes < hz * 5 * (hz < 100 ? .8 : .45), "PERF-03: the frame clock is not kept at display rate");
        }
    }

    // ---- PERF-09: downloads icon ---------------------------------------------------------------------
    {
        Chrome c; if (FAILED(c.InitOffscreen(root, 96, 1180, 84))) return 6;
        Motion::State m(false); c.SetMotion(&m);
        c.downloadIcon.Update(true, 0, false, T(0), false); c.downloadIcon.Update(true, .4f, false, T(500), false);
        const std::vector<std::wstring> titles{L"Téléchargement"};
        for (long long ms : {0LL, 100LL, 233LL, 600LL, 900LL, 1300LL}) {
            c.downloadIcon.Settle(T(ms)); c.Paint(titles, 0, L"", false, 0, 0, nullptr, T(ms));
            c.SavePng(png(L"download-" + std::to_wstring(ms)).c_str());
        }
        for (double hz : {60.0, 144.0, 240.0}) {
            DownloadIcon::Model model; model.Update(true, 0, false, T(0), false); model.Update(true, .4f, false, T(500), false);
            DownloadIcon::Pose last{}; bool seen = false;
            auto l = Simulate([&](Motion::Time now) { model.Settle(now); }, [&](Motion::Time now) { return model.NextWake(now); },
                [&](Motion::Time now) { const auto p = model.At(now); const bool same = seen && p.ring == last.ring && p.arrow == last.arrow && p.success == last.success && p.ripple == last.ripple && p.idle == last.idle; last = p; seen = true; return same; },
                T(1000), 5000, hz);
            Print("downloads icon (progress)", hz, l);
            Expect(l.poseRepaints <= 61 * 5, "PERF-09: icon repaints capped at its own 60 poses a second");
        }
    }
    if (baseline) { std::printf("baseline run: captures and measurements only\n"); return 0; }
    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
