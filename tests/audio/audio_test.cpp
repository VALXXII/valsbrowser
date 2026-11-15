// Per-site audio: model checks (volumes, reports, music vs short sounds, rows, keyboard
// steps, toolbar icon motion), the injected script's text, the Lottie meter's frames
// against the static Phosphor glyphs, and offscreen renders of the toolbar and the panel.
// Usage: audio_test.exe <repo root> <output folder>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../src/audio-panel.h"
#include "../../src/audio-script.h"

using namespace NativeChrome;
using namespace NativeChrome::Audio;
static int failures = 0;
static void Expect(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; } }
static Time T0 = Time{} + std::chrono::hours(1);
static Time At(long long ms) { return T0 + Milliseconds(ms); }

static void VolumeChecks() {
    Volumes v;
    Expect(v.Get(L"youtube.com") == 100 && !v.Custom(L"youtube.com"), "default volume is 100 %");
    Expect(v.Set(L"youtube.com", 35) && v.Get(L"youtube.com") == 35 && v.Custom(L"youtube.com"), "set a site's volume");
    Expect(!v.Set(L"youtube.com", 35), "same value is no change");
    Expect(v.Set(L"music.example", 140) == false && v.Get(L"music.example") == 100, "values clamp to 100 (= default, nothing stored)");
    Expect(v.Set(L"quiet.example", -5) && v.Get(L"quiet.example") == 0, "values clamp to 0");
    Expect(v.Set(L"youtube.com", 100) && !v.Custom(L"youtube.com"), "back to 100 % forgets the site");
    Expect(!v.Set(L"", 20), "no site, no volume");
    v.Set(L"youtube.com", 35);
    std::stringstream io; v.Write(io);
    Volumes r; r.Read(io);
    Expect(r.Get(L"youtube.com") == 35 && r.Get(L"quiet.example") == 0 && r.Size() == 2, "volumes round-trip");
    std::stringstream bad("evil\"site\t20\nUPPER.com\t20\nok.com\t20x\nok.com\t\n\tok\nfine.com\t42\r\nlast.com\t100\n");
    Volumes b; b.Read(bad);
    Expect(b.Size() == 1 && b.Get(L"fine.com") == 42, "malformed lines are skipped, CRLF accepted, 100 not stored");
    Expect(SiteKey(L"https://www.YouTube.com/watch?v=1") == L"youtube.com" && SiteKey(L"http://localhost:8080/a") == L"localhost" && SiteKey(L"file:///C:/a.mp3").empty() && SiteKey(L"about:blank").empty(), "site keys match the favicon rule");
}

static void ReportChecks() {
    Report r;
    Expect(ParseReport(L"{\"lisiereAudio\":1,\"p\":1,\"d\":215.3,\"l\":0,\"w\":0,\"v\":1,\"s\":2,\"t\":\"Clair de lune \\u00e9\\\"x\\\"\",\"a\":\"Debussy\",\"b\":\"Suite\",\"i\":\"https://img.example/a.jpg\",\"h\":1}", r), "report parses");
    Expect(r.playing == 1 && std::abs(r.longest - 215.3) < 1e-9 && !r.live && r.session == 2 && r.video == 1, "report numbers");
    Expect(r.title == L"Clair de lune \u00e9\"x\"" && r.artist == L"Debussy" && r.album == L"Suite" && r.artwork == L"https://img.example/a.jpg", "report strings and escapes");
    Expect(ParseReport(L"{\"lisiereAudio\":1,\"p\":0,\"d\":-1,\"l\":1,\"t\":\"  a\\n\\tb  \",\"i\":\"http://insecure/a.png\"}", r) && r.title == L"a b" && r.artwork.empty() && r.live, "titles are cleaned, only https artwork");
    Expect(!ParseReport(L"{\"other\":1}", r) && !ParseReport(L"{\"lisiereAudio\":1,\"t\":\"unterminated}", r) && !ParseReport(L"{\"lisiereAudio\":1,\"p\":}", r), "malformed reports are rejected");
    Expect(ParseReport(L"{\"lisiereAudio\":1,\"p\":1e9,\"s\":7,\"d\":null}", r) && r.playing == 1000 && r.session == 2 && r.longest == -1, "counts and states are bounded");
}

static void ClassificationChecks() {
    // A click: audible for 300 ms, no media evidence -> short sound, then silent.
    TabAudio click; click.site = L"game.example";
    Expect(click.At(At(0)) == Kind::Silent && !click.heard, "nothing heard yet");
    Expect(click.SetAudible(true, At(0)) && click.heard && click.At(At(100)) == Kind::Effect, "a short sound is an effect");
    Expect(click.NextWake(At(100)).delay && *click.NextWake(At(100)).delay == Milliseconds(4900), "wakes when the sound would count as sustained");
    click.SetAudible(false, At(300));
    Expect(click.At(At(400)) == Kind::Silent && click.heard, "after the click: silent but still listed");
    Expect(!click.Forgettable(At(300 + ForgetMs - 1), false) && click.Forgettable(At(300 + ForgetMs), false) && !click.Forgettable(At(300 + ForgetMs), true), "short-sound entries fade after 10 min unless their volume was changed");
    // Sustained sound without media evidence (game music via Web Audio): music after 5 s.
    TabAudio game; game.site = L"game.example"; game.SetAudible(true, At(0)); game.report.webAudio = 1;
    Expect(game.At(At(4999)) == Kind::Effect && game.At(At(5000)) == Kind::Media, "five seconds of continuous sound count as music");
    // A song: long media element -> music at once.
    TabAudio song; song.site = L"music.example"; song.report.playing = 1; song.report.longest = 215; song.SetAudible(true, At(0));
    Expect(song.At(At(1)) == Kind::Media && !song.NextWake(At(1)).delay, "a long media element is music immediately");
    song.report.longest = 2.5;
    Expect(song.At(At(1)) == Kind::Effect, "a 2.5 s clip is a short sound");
    song.report.live = true; Expect(song.At(At(1)) == Kind::Media, "a live stream is music");
    song.report = Report{}; song.report.session = 2; Expect(song.At(At(1)) == Kind::Media, "a Media Session title is music");
    // Navigation to another site starts over.
    song.SiteChanged(L"other.example", At(10)); Expect(song.report.session == 0 && song.heard, "still audible: the new site is heard");
    song.SetAudible(false, At(20)); song.SiteChanged(L"third.example", At(30)); Expect(!song.heard, "a silent tab on a new site is not listed");
    // Toolbar debounce.
    Toolbar bar; bar.Update(true, At(0)); Expect(bar.Music(), "music on immediately");
    bar.Update(false, At(100)); Expect(bar.Music() && bar.NextWake(At(100)).delay && *bar.NextWake(At(100)).delay == Milliseconds(1500), "a short gap keeps the icon playing");
    bar.Update(true, At(900)); bar.Update(false, At(1000)); bar.Update(false, At(2499)); Expect(bar.Music(), "the hold restarts after music returns");
    bar.Update(false, At(2500)); Expect(!bar.Music() && !bar.NextWake(At(2500)).delay, "1.5 s without music stops it");
}

static void RowChecks() {
    Volumes v; v.Set(L"music.example", 40);
    TabAudio a, b, c, d;
    a.site = L"game.example"; a.SetAudible(true, At(0)); a.SetAudible(false, At(200));
    b.site = L"music.example"; b.SetAudible(true, At(1000)); b.report.playing = 1; b.report.longest = 200; b.report.title = L"Song"; b.report.artist = L"Artist"; b.report.artwork = L"https://a/b.jpg";
    c.site = L"music.example"; c.SetAudible(true, At(500)); c.SetAudible(false, At(600));
    d.site = L"silent.example"; // never heard
    std::vector<TabView> tabs{{&a, L"Game  tab", false}, {&b, L"Music tab", true}, {&c, L"Other music tab", false}, {&d, L"Nothing", false}};
    auto rows = Rows(tabs, v, At(1500));
    Expect(rows.size() == 2, "one row per heard site");
    Expect(rows.size() == 2 && rows[0].site == L"game.example" && rows[1].site == L"music.example", "rows in first-heard order (stable)");
    Expect(rows.size() == 2 && rows[0].title == L"Game tab" && rows[0].detail == L"game.example" && rows[0].status == L"Silencieux" && rows[0].kind == Kind::Silent && rows[0].percent == 100, "silent short-sound row");
    Expect(rows.size() == 2 && rows[1].title == L"Song" && rows[1].detail == L"Artist \u00b7 music.example \u00b7 2 onglets" && rows[1].tab == 1 && rows[1].percent == 40 && rows[1].kind == Kind::Media && rows[1].status.empty() && rows[1].artwork == L"https://a/b.jpg", "music row speaks for its playing tab");
    a.SetAudible(true, At(1600));
    rows = Rows(tabs, v, At(1700));
    Expect(rows[0].status == L"Sons courts" && rows[0].kind == Kind::Effect, "short sounds say so, never 'playing'");
    Expect(ButtonName(rows, true) == L"Son des sites : Song (music.example)", "button name names the music");
    a.SetAudible(false, At(1800)); b.SetAudible(false, At(1800));
    rows = Rows(tabs, v, At(1900));
    Expect(rows[1].status == L"En pause", "a silent media site reads as paused");
    Expect(ButtonName(rows, false) == L"Son des sites : 2 sites récents, silencieux", "button name when quiet");
    Expect(KeyStep(50, VK_RIGHT) == 51 && KeyStep(50, VK_DOWN) == 49 && KeyStep(100, VK_UP) == 100 && KeyStep(0, VK_LEFT) == 0 && KeyStep(71, VK_RIGHT) == 72 && KeyStep(50, VK_PRIOR) == 60 && KeyStep(50, VK_NEXT) == 40 && KeyStep(50, VK_HOME) == 0 && KeyStep(50, VK_END) == 100 && KeyStep(50, 'A') == 50, "keyboard steps");
    Expect(ToggleMute(70, 70) == 0 && ToggleMute(0, 70) == 70 && ToggleMute(0, 0) == 100, "mute toggles to 0 and back");
    Expect(PercentText(7) == L"7\u202f%", "value text");
    {   // Ticks only picture the value: nearest tick, never written back; the lane maps to 0..100 continuously.
        namespace L = AudioPanel::Layout;
        Expect(L::NearestTick(72) == 11 && L::NearestTick(0) == 0 && L::NearestTick(100) == 15 && L::NearestTick(3) == 0 && L::NearestTick(4) == 1 && L::NearestTick(70) == 11 && L::NearestTick(69) == 10, "nearest tick (15 intervals)");
        Expect(std::lround(L::PercentAt(L::XAt(72))) == 72 && std::lround(L::PercentAt(L::XAt(73))) == 73 && std::lround(L::PercentAt(L::XAt(1))) == 1, "every percent is reachable by pointer (no snapping)");
        Expect((L::XAt(73) - L::XAt(72)) > 1.9f, "about 2 DIP per percent along the lane");
        Expect(L::PercentAt(0) == 0 && L::PercentAt(400) == 100, "lane clamps to 0..100");
        Expect(std::abs(L::TickLeft(0) - 52.41f) < .05f && std::abs(L::TickLeft(15) + L::tickWidth + L::tickGap - L::valueLeft) < .01f, "Paper THG-0 space-between ticks");
        Expect(L::PopoverHeight(1) == 17 + 72 + 17 && L::PopoverHeight(2) == 211 && L::PopoverHeight(7) == 259, "Paper heights");
    }
}

static void IconChecks() {
    IconModel m; m.Show(true, At(0));
    auto p = m.At(At(0));
    Expect(p.frame == -1 && !p.high && std::abs(p.emphasis - Icon::Subdued) < 1e-6 && p.presence == 0, "first appearance: static, subdued, fading in");
    Expect(m.At(At(Icon::FadeMs)).presence == 1 && !m.NextWake(At(Icon::FadeMs)).delay, "at rest: no wake-up");
    // Short sound: the model never sees music: nothing moves.
    m.Update(false, At(200), false); Expect(!m.Animating() && !m.NextWake(At(200)).delay, "short sounds never animate");
    // Music: intro (96 -> 72) over 400 ms, then the loop.
    m.Update(true, At(1000), false);
    Expect(m.At(At(1000)).frame == Icon::Loop + Icon::Out && m.At(At(1200)).frame == Icon::Loop + Icon::Out / 2, "intro plays the outro backwards");
    Expect(m.At(At(1200)).emphasis > Icon::Subdued && m.At(At(1200)).emphasis < 1, "emphasis rises with the intro");
    m.Settle(At(1400)); p = m.At(At(1400));
    Expect(p.frame == 0 && p.emphasis == 1, "loop starts at frame 0 (speaker-simple-high)");
    static_assert(Icon::LoopMs == 5000, "user-chosen default: the 72-frame loop lasts 5 s");
    Expect(m.At(At(1400 + 2500)).frame == 36 && m.At(At(1400 + 1250)).frame == 18, "loop frame follows time (72 frames over 5 s)");
    Expect(m.At(At(1400 + 1270)).frame == 18 && m.At(At(1400 + 1320)).frame == 19, "one loop frame every ~69 ms");
    auto w = m.NextWake(At(1400 + 1270)); Expect(w.delay && *w.delay == Milliseconds(50) && w.frames, "loop wakes on the next frame boundary");
    // Stop: the cycle finishes (no jump), then the outro to the subdued static glyph.
    m.Update(false, At(1400 + 700), false);
    Expect(m.At(At(1400 + 4900)).frame > 0 && m.At(At(1400 + 4900)).frame < Icon::Loop, "still on the loop until the cycle ends");
    p = m.At(At(1400 + 5000 + 200)); Expect(p.frame == Icon::Loop + Icon::Out / 2, "outro after the cycle");
    m.Settle(At(1400 + 5000 + 400)); p = m.At(At(1400 + 5000 + 400));
    Expect(p.frame == -1 && !p.high && !m.Animating() && !m.NextWake(At(6900)).delay, "back to the static icon, no timer");
    // Interrupt the intro: reverses from where it is.
    m.Update(true, At(10000), false); m.Update(false, At(10100), false);
    Expect(m.At(At(10100)).frame == Icon::Loop + Icon::Out - 6 && m.At(At(10150)).frame == Icon::Loop + Icon::Out - 3 && m.At(At(10200)).frame == -1, "intro interrupted: goes back from its current frame");
    // Restart during the cycle's end: keeps looping, no visible change.
    m.Update(true, At(11000), false); m.Settle(At(11400)); m.Update(false, At(11500), false); m.Update(true, At(11600), false);
    Expect(m.At(At(15700)).frame >= 0 && m.At(At(15700)).frame < Icon::Loop && m.At(At(15700)).emphasis == 1, "music back before the cycle ended: still looping");
    // Reduced motion: static glyphs, swapped at once.
    IconModel r; r.Show(true, At(0)); r.Update(true, At(0), true);
    p = r.At(At(0)); Expect(p.frame == -1 && p.high && p.emphasis == 1 && p.presence == 1 && !r.NextWake(At(0)).delay, "reduced motion: static music glyph, no fade");
    r.Update(false, At(10), true); p = r.At(At(10)); Expect(p.frame == -1 && !p.high && p.emphasis == Icon::Subdued, "reduced motion: static subdued glyph");
}

static void ScriptChecks(const std::wstring& out) {
    Volumes v; v.Set(L"youtube.com", 35); v.Set(L"a.example", 5); v.Set(L"b.example", 0);
    const auto s = Script(v);
    Expect(s.find(L"const START={\"a.example\":0.05,\"b.example\":0,\"youtube.com\":0.35};") != std::wstring::npos, "starting gains baked in");
    Expect(s.find(L"__GAINS__") == std::wstring::npos, "placeholder replaced");
    Expect(GainMessage(35) == L"{\"lisiereAudioGain\":0.35}" && GainMessage(100) == L"{\"lisiereAudioGain\":1}" && GainMessage(0) == L"{\"lisiereAudioGain\":0}" && GainMessage(7) == L"{\"lisiereAudioGain\":0.07}", "gain messages");
    std::ofstream f(std::filesystem::path(out + L"\\audio-script.js"), std::ios::binary); f << Downloads::ToUtf8(s);
}

// Pixel distance between a Lottie frame and the same glyph drawn as a path (alpha only).
struct GlyphCompare {
    std::wstring root; double worst = 0;
    double Compare(int frame, const char* path) {
        Lottie::Asset a; if (!a.Load(root + L"\\assets\\lottie\\audio-meter.json")) return 1e9;
        if (a.FrameCount() != Icon::Frames) return 1e9;
        const unsigned side = 48; const auto* f = a.Sample(double(frame) / (Icon::Frames - 1), side); if (!f) return 1e9;
        Chrome c; if (FAILED(c.InitOffscreen(root, 96, side, side))) return 1e9;
        auto* t = c.RenderTarget(); t->BeginDraw(); t->Clear(D2D1::ColorF(0, 0, 0, 0)); c.PathGlyph(path, {0, 0, float(side), float(side)}, float(side), D2D1::ColorF(1, 1, 1, 1)); t->EndDraw();
        double sum = 0;
        for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
            const double la = (f->bgra[y * side + x] >> 24) / 255.0, pa = (c.SurfacePixel(float(x) + .5f, float(y) + .5f) >> 24) / 255.0;
            sum += std::abs(la - pa);
        }
        return sum / (side * side);
    }
};

struct Renders {
    std::wstring root, out;
    std::shared_ptr<FaviconImage> Art() {
        auto img = std::make_shared<FaviconImage>(); img->width = img->height = 96; img->stride = 96 * 4; img->pixels.resize(img->stride * 96);
        for (unsigned y = 0; y < 96; ++y) for (unsigned x = 0; x < 96; ++x) { auto* p = &img->pixels[y * img->stride + x * 4]; p[0] = static_cast<unsigned char>(120 + x); p[1] = static_cast<unsigned char>(40 + y); p[2] = static_cast<unsigned char>(200 - y / 2); p[3] = 255; }
        return img;
    }
    std::vector<AudioPanel::Item> Items() {
        std::vector<AudioPanel::Item> items(3);
        items[0].row.site = L"music.example"; items[0].row.title = L"Clair de lune (Suite bergamasque, L. 75)"; items[0].row.artist = L"Claude Debussy"; items[0].row.detail = L"Claude Debussy \u00b7 music.example"; items[0].row.kind = Kind::Media; items[0].row.percent = 72; items[0].artwork = Art();
        items[1].row.site = L"game.example"; items[1].row.title = L"Puzzle du jour"; items[1].row.detail = L"game.example"; items[1].row.status = L"Sons courts"; items[1].row.kind = Kind::Effect; items[1].row.percent = 35;
        items[2].row.site = L"news.example"; items[2].row.title = L"Le journal de 20 h"; items[2].row.detail = L"news.example"; items[2].row.status = L"En pause"; items[2].row.percent = 0;
        return items;
    }
    bool Panel(const wchar_t* name, UINT dpi, std::vector<AudioPanel::Item> items, int focus, bool keyboard, int hover = 0, int dragging = -1, bool drawer = false, float height = 0) {
        namespace L = AudioPanel::Layout;
        Chrome chrome; const float h = height > 0 ? height : L::PopoverHeight(static_cast<int>(items.size()));
        if (FAILED(chrome.InitOffscreen(root, dpi, L::width, h))) return false;
        Motion::State motion(true); if (hover) motion.SetInteraction(hover, 0, Motion::Now());
        chrome.SetMotion(&motion); chrome.keyboardModality = keyboard;
        AudioPanel::View view(chrome); AudioPanel::Scene s; s.items = &items; s.motion = &motion; s.focus = focus; s.keyboard = keyboard; s.dragging = dragging; s.now = Motion::Now(); s.drawer = drawer; s.height = h;
        chrome.RenderTarget()->BeginDraw(); view.Paint(s); if (FAILED(chrome.RenderTarget()->EndDraw())) return false;
        // Mute glyph ink: accent at 100 %, the usual 50 % white otherwise, never brighter at 0 % / muted.
        for (int i = 0; i < static_cast<int>(items.size()) && (drawer || i < L::popoverRows); ++i) {
            const auto m = L::Mute(L::RowTop(i, drawer)); UINT32 best = 0; int bestSum = -1;
            for (float y = m.top + 4; y < m.bottom - 4; y += .5f) for (float x = m.left + 4; x < m.right - 4; x += .5f) {
                const UINT32 c = chrome.SurfacePixel(x, y); const int sum = static_cast<int>((c & 0xff) + ((c >> 8) & 0xff) + ((c >> 16) & 0xff));
                if (sum > bestSum) { bestSum = sum; best = c; } }
            const int b = best & 0xff, g = (best >> 8) & 0xff, r = (best >> 16) & 0xff;
            if (items[i].row.percent >= 100) Expect(b > 200 && b - r > 100, "mute glyph is the accent at 100 %");
            else Expect(std::max({r, g, b}) <= 150 && std::abs(r - b) < 12, items[i].row.percent == 0 ? "mute glyph at 0 % / muted is not bright white" : "mute glyph keeps the 50 % ink in between");
        }
        // Hovering the mute button: accent tint at 100 %, grey ghost surface otherwise.
        if (hover && AudioPanel::HitPart(hover) == 1) {
            const int i = AudioPanel::HitRow(hover); const auto m = L::Mute(L::RowTop(i, drawer));
            const UINT32 c = chrome.SurfacePixel(m.left + 3, (m.top + m.bottom) / 2); const int b = c & 0xff, rr = (c >> 16) & 0xff;
            if (items[i].row.percent >= 100) Expect(b - rr > 20, "mute hover fill is the accent tint at 100 %");
            else Expect(std::abs(b - rr) < 4 && b > 0x20, "mute hover fill is the grey surface below 100 %");
        }
        // Hovering a row's title/metadata paints nothing (no plate): the row area stays the canvas.
        if (hover && AudioPanel::HitPart(hover) == 0) {
            const UINT32 c = chrome.SurfacePixel(L::right + 2, L::RowTop(AudioPanel::HitRow(hover), drawer) + 2);
            Expect((c & 0xff) == 0x17 && ((c >> 8) & 0xff) == 0x17, "no hover fill on a row's title area");
        }
        // The tick nearest to the value is the tall accent one; before it lit, after it dim. The value is untouched.
        if (!items.empty() && items[0].row.percent > 0) {
            const float top = L::RowTop(0, drawer), y = top + L::controlTop + L::control / 2;
            const int k = L::NearestTick(items[0].row.percent);
            const UINT32 tip = chrome.SurfacePixel(L::TickCenter(k), y - 7.4f), body = chrome.SurfacePixel(L::TickCenter(k), y);
            Expect((body & 0xff) > 180 && ((body >> 16) & 0xff) < 140, "pictured tick is the accent");
            Expect((tip & 0xff) > 90, "pictured tick is 16 DIP tall (12 for the others)");
            if (k < L::ticks - 1) { const UINT32 next = chrome.SurfacePixel(L::TickCenter(k + 1), y); Expect((next & 0xff) < 70, "ticks after the value are dim"); }
            if (k > 0) { const UINT32 before = chrome.SurfacePixel(L::TickCenter(k - 1), y); Expect((before & 0xff) > 180, "ticks before the value are lit"); }
        }
        return SUCCEEDED(chrome.SavePng((out + L"\\" + name).c_str()));
    }
    bool Toolbar(const wchar_t* name, UINT dpi, bool shown, int frame, bool music, float emphasis, int focus = 0) {
        Chrome chrome; if (FAILED(chrome.InitOffscreen(root, dpi, 1180, 84))) return false;
        Motion::State motion(true); chrome.SetMotion(&motion); chrome.keyboardModality = focus != 0;
        chrome.audioShown = shown; chrome.audioIcon.Show(shown, Time{});
        chrome.audioIcon.Update(music, Time{}, true); (void)frame; (void)emphasis;
        chrome.Paint({L"Musique", L"Jeu"}, 0, L"", false, 0, focus);
        if (shown) {
            const auto r = std::find_if(chrome.regions.begin(), chrome.regions.end(), [](const Region& x) { return x.id == 'V'; });
            Expect(r != chrome.regions.end() && r->rect.left == 1180 - 127 && r->rect.right == 1180 - 103, "audio button left of History, 6 DIP gap");   // PR76: no toolbar private button, the cluster moved 30 DIP right
            Expect(chrome.bar.right == 1180 - 133, "address field ends 6 DIP before the audio button");
            const auto y = std::find_if(chrome.regions.begin(), chrome.regions.end(), [](const Region& x) { return x.id == 'Y'; });
            Expect(y != chrome.regions.end() && y->rect.left == 1180 - 97, "History does not move");
        } else {
            Expect(chrome.bar.right == 1180 - 103 && std::none_of(chrome.regions.begin(), chrome.regions.end(), [](const Region& x) { return x.id == 'V'; }), "no audio button before any sound");
        }
        return SUCCEEDED(chrome.SavePng((out + L"\\" + name).c_str()));
    }
    bool Frames(const wchar_t* name) {
        // A strip of the meter: loop frames 0..72 every 8, then the outro.
        const int frames[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 78, 84, 90, 96};
        const float cell = 40; Chrome chrome; if (FAILED(chrome.InitOffscreen(root, 192, cell * 14, cell))) return false;
        auto* t = chrome.RenderTarget(); t->BeginDraw(); const float c = Design::Surface::canvas; t->Clear(D2D1::ColorF(c, c, c, 1));
        bool ok = true;
        for (int i = 0; i < 14; ++i) ok = chrome.DrawAudioFrame(frames[i], {i * cell, 0, (i + 1) * cell, cell}, 24, D2D1::ColorF(0x3E / 255.f, 0x67 / 255.f, 1, 1)) && ok;
        t->EndDraw();
        Expect(ok, "every meter frame renders");
        return SUCCEEDED(chrome.SavePng((out + L"\\" + name).c_str()));
    }
};

int wmain(int argc, wchar_t** argv) {
    VolumeChecks(); ReportChecks(); ClassificationChecks(); RowChecks(); IconChecks();
    if (argc >= 3) {
        const std::wstring root = argv[1], out = argv[2];
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
        ScriptChecks(out);
        {
            GlyphCompare g{root};
            const double high = g.Compare(0, AudioPanel::Glyph::simpleHigh), low = g.Compare(Icon::Frames - 1, AudioPanel::Glyph::simpleLow), loop = g.Compare(Icon::Loop, AudioPanel::Glyph::simpleHigh);
            std::printf("meter vs glyph (mean alpha error): frame0=%.4f frame72=%.4f frame96=%.4f\n", high, loop, low);
            Expect(high < .03 && loop < .03, "meter frame 0/72 is speaker-simple-high");
            Expect(low < .03, "meter frame 96 is speaker-simple-low");
            const double beat = g.Compare(12, AudioPanel::Glyph::simpleHigh); std::printf("meter at its first beat (frame 12): %.4f\n", beat);
            Expect(beat > high + .004, "the meter moves between rests (subtly)");
        }
        Renders r{root, out};
        auto items = r.Items();
        Expect(r.Panel(L"panel-96.png", 96, items, 0, false), "render panel 100 %");
        Expect(r.Panel(L"panel-150.png", 144, items, 0, false), "render panel 150 %");
        Expect(r.Panel(L"panel-focus-slider.png", 144, items, AudioPanel::SliderHit(0), true), "render keyboard focus on slider");
        Expect(r.Panel(L"panel-focus-mute.png", 144, items, AudioPanel::MuteHit(2), true), "render keyboard focus on mute");
        Expect(r.Panel(L"panel-hover-row.png", 144, items, 0, false, AudioPanel::RowHit(1)), "render row hover");
        Expect(r.Panel(L"panel-drag.png", 144, items, AudioPanel::SliderHit(0), false, 0, 0), "render dragging");
        Expect(r.Panel(L"panel-empty.png", 144, {}, 0, false), "render empty");
        {   // 100 %: the last tick lights, the value keeps its column.
            auto full = items; full[0].row.percent = 100;
            Expect(r.Panel(L"panel-full.png", 144, full, 0, false, AudioPanel::SliderHit(0)), "render 100 %");
            Expect(r.Panel(L"panel-full-mute-hover.png", 144, full, 0, false, AudioPanel::MuteHit(0)), "render 100 % mute hover");
            Expect(AudioPanel::Layout::TickLeft(15) + AudioPanel::Layout::tickWidth + 6 <= AudioPanel::Layout::valueLeft, "the last tick and its glow keep clear of the value");
        }
        {   // Seven sites: two rows + « Voir tout (7) » (Paper TGK-0), then the drawer (U40-0).
            auto many = items; while (many.size() < 7) { auto copy = items[1]; copy.row.site = L"site" + std::to_wstring(many.size()) + L".example"; copy.row.percent = 72; many.push_back(copy); }
            many[3].row.percent = 100; // the four ink states in one drawer: 72, 35, 0 (muted), 100
            Expect(r.Panel(L"panel-view-all.png", 144, many, 0, false), "render popover with Voir tout");
            Expect(r.Panel(L"panel-view-all-focus.png", 144, many, AudioPanel::ViewAllHit, true), "render Voir tout keyboard focus");
            Expect(r.Panel(L"drawer.png", 144, many, 0, false, 0, -1, true, 734), "render drawer");
            Expect(r.Panel(L"drawer-info-focus.png", 144, many, AudioPanel::InfoHit(1), true, 0, -1, true, 734), "render drawer info focus");
        }
        Expect(r.Toolbar(L"toolbar-none.png", 144, false, -1, false, 1), "render toolbar without audio");
        Expect(r.Toolbar(L"toolbar-sfx.png", 144, true, -1, false, Icon::Subdued), "render toolbar after short sounds");
        Expect(r.Toolbar(L"toolbar-music-reduced.png", 144, true, -1, true, 1), "render toolbar music (reduced motion)");
        Expect(r.Toolbar(L"toolbar-focus.png", 144, true, -1, true, 1, 'V'), "render toolbar keyboard focus");
        Expect(r.Frames(L"meter-frames.png"), "render meter frames");
        CoUninitialize();
    }
    if (failures) { std::fprintf(stderr, "%d audio check(s) failed\n", failures); return 1; }
    std::printf("audio checks passed\n");
    return 0;
}
