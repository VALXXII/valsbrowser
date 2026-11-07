// Native-only tests for the update toast: model, motion, geometry/DPI, hit testing,
// keyboard, popup window behaviour and Direct2D PNG snapshots. No WebView2, no network.
// Usage: toast-tests.exe <repoRoot> <snapshotDir>
#include "../../src/update-toast.cpp"
#include <wincodec.h>
#include <cstdio>
#include <vector>

using namespace BrowserUpdate;
using namespace BrowserUpdate::ToastModel;
namespace M = NativeChrome::Motion;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool Near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }
static M::Time T0() { return M::Time{} + std::chrono::hours(1); }
static M::Time At(double ms) { return T0() + std::chrono::duration_cast<M::Clock::duration>(std::chrono::duration<double, std::milli>(ms)); }

static View V(Phase p, std::uint64_t received = 0, std::uint64_t total = 0, double eta = -1, std::wstring error = L"") {
    View v; v.phase = p; v.received = received; v.total = total; v.secondsRemaining = eta; v.error = std::move(error); return v;
}

static void ModelTests() {
    // Paper texts verbatim.
    auto a = Describe(V(Phase::Available));
    CHECK(a.title == L"New update available" && a.body == L"A new update is ready to be downloaded." && a.buttons.size() == 1 && a.buttons[0].label == L"Download" && a.buttons[0].action == Action::Download);
    auto d = Describe(V(Phase::Downloading, 52848230, 209715200, 14));
    CHECK(d.title == L"Downloading..." && d.buttons[0].label == L"Cancel" && d.buttons[0].action == Action::Cancel);
    CHECK(d.size == L"50.4/200MB");
    CHECK(d.eta == L"00:14");
    auto r = Describe(V(Phase::Ready));
    CHECK(r.title == L"Ready for install" && r.body == L"The update is ready to be installed." && r.buttons[0].label == L"Restart and install" && r.buttons[0].action == Action::RestartInstall);
    auto i = Describe(V(Phase::Installed));
    CHECK(i.title == L"Update installed" && i.body == L"The new update has been successfully installed." && i.buttons.empty());
    CHECK(Describe(V(Phase::Hidden)).kind == Kind::None && Describe(V(Phase::Checking)).kind == Kind::None);
    auto e = Describe(V(Phase::Error, 0, 0, -1, L"Échec du téléchargement"));
    CHECK(e.kind == Kind::Error && e.body == L"Échec du téléchargement" && e.buttons.size() == 2 && e.buttons[1].action == Action::Retry && e.buttons[0].action == Action::Dismiss);
    CHECK(Describe(V(Phase::Error)).body == L"The update could not be completed.");
    CHECK(Describe(V(Phase::Installing)).buttons.empty());
    // Never invented: unknown total and ETA.
    auto u = Describe(V(Phase::Downloading, 3 * 1048576 + 300000, 0, -1));
    CHECK(u.size == L"3.3MB" && u.eta.empty() && u.progress < 0);
    CHECK(FormatEta(-1).empty() && FormatEta(NAN).empty() && FormatEta(0) == L"00:00" && FormatEta(59.2) == L"01:00" && FormatEta(3725) == L"1:02:05");
    CHECK(FormatSize(0, 209715200) == L"0/200MB" && FormatSize(300 * 1048576ull, 200 * 1048576ull) == L"200/200MB");
    CHECK(FormatSize(512ull * 1048576, 1536ull * 1048576) == L"0.5/1.5GB");
    CHECK(Describe(V(Phase::Downloading, 50, 100)).progress == .5f);
    // Paper geometry: exact card heights with two-line bodies.
    CHECK(Near(Layout(a, 269, 2).height, Paper::available));
    CHECK(Near(Layout(d, 269, 0).height, Paper::downloading));
    CHECK(Near(Layout(r, 269, 2).height, Paper::ready));
    CHECK(Near(Layout(i, 269, 2).height, Paper::installed));
    auto ga = Layout(a, 269, 2);
    CHECK(Near(ga.buttons[0].left, 17) && Near(ga.buttons[0].right, 252) && Near(ga.buttons[0].Height(), 32));
    CHECK(Near(ga.title.left, 17 + 16 + 6) && Near(ga.icon.top, 17) && Near(ga.body.top, 49));
    CHECK(Near(ContentWidth(269), 235));
    auto gd = Layout(d, 269, 0);
    CHECK(Near(gd.track.Height(), 4) && Near(gd.track.Width(), 235) && Near(gd.track.top, gd.stats.bottom + 8));
    auto ge = Layout(e, 269, 1);
    CHECK(ge.buttons.size() == 2 && Near(ge.buttons[1].left - ge.buttons[0].right, 8) && Near(ge.buttons[0].Width(), ge.buttons[1].Width()));
    // Responsive width.
    CHECK(FitWidth(1180, 14, 0) == 269 && FitWidth(260, 14, 0) == 232 && FitWidth(250, 14, 0) == 0 && FitWidth(600, 14, 300) == 269 && FitWidth(520, 14, 270) == 0);
    CHECK(FitsHeight(815, 84, Paper::available) && !FitsHeight(250, 84, Paper::available));
}

static void MotionTests() {
    Presenter p; p.SetReduced(false, T0());
    auto a = Describe(V(Phase::Available)); auto ga = Layout(a, 269, 2);
    CHECK(!p.Moving(T0()) && !p.Sample(T0()).visible);
    p.Set(a, ga, T0());
    auto f0 = p.Sample(T0());
    CHECK(f0.visible && Near(f0.opacity, 0) && Near(f0.offset, Timing::enterOffset) && Near(f0.height, Paper::available));
    auto f300 = p.Sample(At(300));
    CHECK(f300.opacity > .99f && f300.offset < .1f);
    // Monotonic, no overshoot on entrance.
    float lastOpacity = -1, lastOffset = 100; bool monotonic = true;
    for (int ms = 0; ms <= 600; ++ms) {
        auto f = p.Sample(At(ms));
        if (f.opacity < lastOpacity - 1e-6f || f.offset > lastOffset + 1e-6f || f.offset < -1e-6f || f.opacity > 1) monotonic = false;
        lastOpacity = f.opacity; lastOffset = f.offset;
    }
    CHECK(monotonic);
    CHECK(p.Moving(At(200)) && !p.Moving(At(400)));
    p.Settle(At(400));
    CHECK(!p.Moving(At(400)) && !p.Moving(At(100000))); // idle: zero frame requests
    // State swap keeps identity; height springs, bottom-anchored, outgoing fades in 150 ms.
    auto d = Describe(V(Phase::Downloading, 52848230, 209715200, 14)); auto gd = Layout(d, 269, 0);
    const unsigned gen = p.Generation();
    p.Set(d, gd, At(1000));
    CHECK(p.Generation() != gen);
    auto s0 = p.Sample(At(1000));
    CHECK(s0.visible && Near(s0.height, Paper::available) && s0.layers[0].content && s0.layers[0].content->kind == Kind::Available && Near(s0.layers[0].opacity, 1) && Near(s0.layers[1].opacity, 0) && Near(s0.opacity, 1));
    CHECK(Near(s0.layers[1].iconScale, .25f));
    auto s150 = p.Sample(At(1150));
    CHECK(!s150.layers[0].content && s150.layers[1].opacity > .75f && p.Sample(At(1250)).layers[1].opacity > .95f);
    bool clean = true; // two texts are never legible at the same time
    for (int ms = 0; ms <= 150; ++ms) { auto f = p.Sample(At(1000 + ms)); if (f.layers[0].content && std::min(f.layers[0].opacity, f.layers[1].opacity) > .2f) clean = false; }
    CHECK(clean);
    auto s300 = p.Sample(At(1300));
    CHECK(std::fabs(s300.height - Paper::downloading) < .3f);
    bool between = true;
    for (int ms = 0; ms <= 600; ++ms) { const float h = p.Sample(At(1000 + ms)).height; if (h > Paper::available + 1e-3f || h < Paper::downloading - 1e-3f) between = false; }
    CHECK(between);
    // Interruption mid-swap: continuous from the presentation value.
    const float hMid = p.Sample(At(1100)).height;
    auto r = Describe(V(Phase::Ready)); auto gr = Layout(r, 269, 2);
    p.Set(r, gr, At(1100));
    CHECK(Near(p.Sample(At(1100)).height, hMid, 1e-3));
    CHECK(std::fabs(p.Sample(At(1101)).height - hMid) < .5f);
    for (int ms = 0; ms <= 600; ++ms) { const float h = p.Sample(At(1100 + ms)).height; if (h > Paper::ready + 1e-3f) between = false; }
    CHECK(between);
    p.Settle(At(2000));
    // Progress never exceeds the real fraction.
    p.Set(d, gd, At(3000));
    p.Settle(At(4000));
    auto d2 = Describe(V(Phase::Downloading, 150, 200, 3)); p.Set(d2, gd, At(4000));
    bool capped = true;
    for (int ms = 0; ms <= 600; ++ms) if (p.Sample(At(4000 + ms)).progress > .75f + 1e-6f) capped = false;
    CHECK(capped && Near(p.Sample(At(4600)).progress, .75f, 1e-3));
    auto d3 = Describe(V(Phase::Downloading, 10, 200)); p.Set(d3, gd, At(5000)); // restart snaps back
    CHECK(Near(p.Sample(At(5000)).progress, .05f));
    auto du = Describe(V(Phase::Downloading, 10, 0)); p.Set(du, gd, At(5100));
    CHECK(p.Sample(At(5100)).progress < 0);
    p.Settle(At(6000));
    // Exit: 150 ms ease-out, leaves by the entrance path, not interactive.
    p.Set({}, {}, At(7000));
    CHECK(!p.Interactive() && p.Sample(At(7000)).visible);
    auto x75 = p.Sample(At(7075));
    CHECK(x75.opacity < .5f && x75.offset > 0 && x75.offset < Timing::exitOffset);
    CHECK(!p.Sample(At(7150)).visible);
    // Re-show during exit resumes from the presentation value.
    Presenter q; q.SetReduced(false, T0()); q.Set(a, ga, T0()); q.Settle(At(500));
    q.Set({}, {}, At(1000));
    const auto mid = q.Sample(At(1070));
    q.Set(a, ga, At(1070));
    const auto back = q.Sample(At(1070));
    CHECK(q.Interactive() && Near(back.opacity, mid.opacity, 1e-3) && Near(back.offset, mid.offset, 1e-3));
    CHECK(q.Sample(At(1400)).opacity > .99f);
    // Reduced motion: opacity only.
    Presenter rm; rm.SetReduced(true, T0()); rm.Set(a, ga, T0());
    bool still = true;
    for (int ms = 0; ms <= 400; ms += 5) if (!Near(rm.Sample(At(ms)).offset, 0)) still = false;
    CHECK(still && rm.Sample(At(100)).opacity > 0 && rm.Sample(At(100)).opacity < 1 && Near(rm.Sample(At(200)).opacity, 1));
    rm.Settle(At(300));
    rm.Set(d, gd, At(400));
    CHECK(Near(rm.Sample(At(400)).height, Paper::downloading) && Near(rm.Sample(At(400)).layers[1].iconScale, 1));
    rm.Settle(At(1000)); rm.Set({}, {}, At(1000));
    CHECK(Near(rm.Sample(At(1100)).offset, 0) && !rm.Sample(At(1150)).visible);
    // Channel: retarget carries velocity yet never crosses the target.
    Channel c; c.Snap(0); c.Spring(100, T0(), .25);
    const float v = c.Velocity(At(40));
    c.Spring(60, At(40), .25);
    CHECK(Near(c.Velocity(At(40)), v, 1e-2));
    bool noCross = true; for (int ms = 40; ms < 800; ++ms) if (c.Value(At(ms)) > 60 + 1e-3f && c.Value(At(40)) <= 60) noCross = false;
    CHECK(noCross);
}

static void ClickAndDwellTests() {
    auto a = Describe(V(Phase::Available)); auto d = Describe(V(Phase::Downloading, 1, 2));
    Clicks c;
    c.Down(0, 1);
    CHECK(c.Up(0, 2, d) == std::nullopt);        // phase changed under the finger: no Cancel
    c.Down(0, 1);
    CHECK(c.Up(-1, 1, a) == std::nullopt);       // released outside
    c.Down(0, 1);
    CHECK(c.Up(0, 1, a) == Action::Download);
    c.Down(0, 1);
    CHECK(c.Up(0, 1, a) == std::nullopt);        // repeat ignored for the same generation
    CHECK(c.Activate(0, 2, d) == Action::Cancel);
    Dwell w; w.Start(T0());
    CHECK(!w.Expired(At(5999)) && w.Expired(At(6000)));
    w.Hold(true, At(5000));
    CHECK(!w.Expired(At(9000)) && !w.Deadline());
    w.Hold(false, At(9000));
    CHECK(!w.Expired(At(10999)) && w.Expired(At(11000)));
}

// ---- Direct2D / DirectWrite --------------------------------------------------------
static std::wstring repo, snapshots;
static IWICImagingFactory* wic = nullptr;

struct Canvas {
    IWICBitmap* bitmap = nullptr; ID2D1RenderTarget* rt = nullptr; UINT w = 0, h = 0; float dpi = 96;
    bool Create(Painter& painter, float wDip, float hDip, float dpiValue) {
        dpi = dpiValue; w = (UINT)std::lround(wDip * dpi / 96); h = (UINT)std::lround(hDip * dpi / 96);
        if (FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap))) return false;
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
        if (FAILED(painter.factory->CreateWicBitmapRenderTarget(bitmap, props, &rt))) return false;
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        return SUCCEEDED(painter.Bind(rt));
    }
    ~Canvas() { Drop(rt); Drop(bitmap); }
    std::vector<BYTE> Pixels() {
        std::vector<BYTE> px(w * h * 4);
        WICRect r{0, 0, (INT)w, (INT)h};
        bitmap->CopyPixels(&r, w * 4, (UINT)px.size(), px.data());
        return px;
    }
    bool Save(const std::wstring& name) {
        IWICStream* stream = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* frame = nullptr;
        HRESULT hr = wic->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename((snapshots + L"\\" + name).c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = enc->Commit();
        Drop(frame); Drop(enc); Drop(stream);
        return SUCCEEDED(hr);
    }
};

// Background: Paper artboard canvas, or a light page with text-like stripes (legibility check).
static void Background(Painter& p, float w, float h, bool light) {
    p.target->Clear(light ? D2D1::ColorF(.96f, .96f, .96f, 1) : D2D1::ColorF(23 / 255.f, 23 / 255.f, 23 / 255.f, 1));
    if (!light) return;
    p.Color(.25f, 1);
    for (float y = 8; y < h; y += 14)
        for (float x = 8; x < w; x += 46) p.target->FillRectangle(D2D1::RectF(x, y, x + 34 + std::fmod(x * 7, 9.f), y + 6), p.brush);
}
static void PaintAt(Painter& p, const Frame& f, float x, float bottom, float width, const ButtonVisual vis[2], bool reduced) {
    D2D1_MATRIX_3X2_F old; p.target->GetTransform(&old);
    // Same pixel snapping as the popup: bottom/left on whole device pixels, height rounded.
    p.target->SetTransform(D2D1::Matrix3x2F::Translation(x, bottom - f.height) * old);
    p.Paint(f, width, f.height, vis, reduced);
    p.target->SetTransform(old);
}

static Frame Settled(Presenter& pr, Painter& painter, const View& v, float width, bool reduced = false) {
    pr = Presenter(); pr.SetReduced(reduced, T0());
    auto c = Describe(v); pr.Set(c, painter.Measure(c, width), T0()); pr.Settle(At(1000));
    return pr.Sample(At(1000));
}

static void PainterTests(Painter& painter) {
    // Balanced wrapping reproduces the Paper line breaks (2 lines each at 235 DIP).
    for (auto* body : {L"A new update is ready to be downloaded.", L"The update is ready to be installed.", L"The new update has been successfully installed."}) {
        int lines = 0; const float w = painter.BalancedWidth(body, 235, &lines);
        CHECK(lines == 2 && w < 235 && painter.Lines(Role::Body, body, w) == 2);
    }
    // Every title / label fits on one line down to the minimum card width: no clipped glyphs.
    for (auto* title : {L"New update available", L"Downloading...", L"Ready for install", L"Update installed", L"Installing...", L"Update failed"}) {
        const float room = ContentWidth(MinWidth) - Paper::titleIcon - Paper::titleGap;
        CHECK(painter.NaturalWidth(Role::Title, title) <= room);
    }
    CHECK(painter.NaturalWidth(Role::Button, L"Restart and install") <= ContentWidth(MinWidth) - 2 * Paper::buttonPadX);
    CHECK(painter.NaturalWidth(Role::Button, L"Dismiss") <= (ContentWidth(MinWidth) - 8) / 2 - 2 * Paper::buttonPadX);
    // Measured heights equal Paper's.
    CHECK(Near(painter.Measure(Describe(V(Phase::Available)), 269).height, Paper::available));
    CHECK(Near(painter.Measure(Describe(V(Phase::Ready)), 269).height, Paper::ready));
    CHECK(Near(painter.Measure(Describe(V(Phase::Installed)), 269).height, Paper::installed));
    CHECK(Near(painter.Measure(Describe(V(Phase::Downloading, 1, 2, 3)), 269).height, Paper::downloading));
    // ETA is dropped rather than overflowing on a narrow card.
    auto big = Describe(V(Phase::Downloading, 1023ull * 1048576 * 1024, 1024ull * 1048576 * 1024, 359999));
    CHECK(painter.EtaFits(Describe(V(Phase::Downloading, 52848230, 209715200, 14)), 235));
    CHECK(!painter.EtaFits(big, 120));
    // Text cache stays bounded.
    for (int i = 0; i < 200; ++i) painter.Text(Role::Body, std::to_wstring(i), 100);
    CHECK(painter.cache.size() <= Painter::CacheLimit);
}

static void SnapshotTests(Painter& painter) {
    struct Case { const wchar_t* name; View view; };
    const Case cases[] = {
        {L"available", V(Phase::Available)},
        {L"downloading", V(Phase::Downloading, 52848230, 209715200, 14)},
        {L"ready", V(Phase::Ready)},
        {L"installed", V(Phase::Installed)},
        {L"installing", V(Phase::Installing)},
        {L"error", V(Phase::Error, 0, 0, -1, L"Impossible de vérifier le paquet de mise à jour.")},
        {L"downloading-unknown-total", V(Phase::Downloading, 3 * 1048576 + 300000, 0, -1)},
    };
    const ButtonVisual rest[2]{};
    for (float dpi : {96.f, 120.f, 144.f, 192.f}) {
        for (const Case& c : cases) {
            Presenter pr; const Frame f = Settled(pr, painter, c.view, 269);
            // Transparent canvas: everything outside the rounded card must be click-through (alpha 0).
            Canvas t; CHECK(t.Create(painter, 269, f.height, dpi));
            t.rt->BeginDraw(); t.rt->Clear(D2D1::ColorF(0, 0, 0, 0)); painter.Paint(f, 269, f.height, rest, false); CHECK(SUCCEEDED(t.rt->EndDraw()));
            auto px = t.Pixels();
            CHECK(px[3] == 0 && px[(t.w - 1) * 4 + 3] == 0 && px[((t.h - 1) * t.w) * 4 + 3] == 0);       // corners
            const UINT cx = t.w / 2, cy = t.h / 2;
            CHECK(px[(cy * t.w + cx) * 4 + 3] >= 200);                                                       // material
            // Bright (text/icon) pixels exist and stay inside the padding box: no clipped glyphs.
            const int s = (int)std::lround(dpi / 96 * 3);
            int outside = 0, inside = 0;
            for (UINT y = 0; y < t.h; ++y) for (UINT x = 0; x < t.w; ++x) {
                const BYTE g = px[(y * t.w + x) * 4 + 1];
                if (g > 90) { if ((int)x < s || (int)y < s || x >= t.w - s || y >= t.h - s) ++outside; else ++inside; }
            }
            CHECK(outside == 0 && inside > 200);
            if (dpi == 96 || dpi == 144) {
                Canvas k; const float W = 297, H = f.height + 28;
                for (bool light : {false, true}) {
                    if (!k.Create(painter, W, H, dpi)) { CHECK(false); break; }
                    k.rt->BeginDraw(); Background(painter, W, H, light);
                    PaintAt(painter, f, 14, H - 14, 269, rest, false);
                    CHECK(SUCCEEDED(k.rt->EndDraw()));
                    wchar_t name[96]; std::swprintf(name, 96, L"%ls-%ls%d.png", c.name, light ? L"light-" : L"", (int)dpi);
                    CHECK(k.Save(name));
                    Drop(k.rt); Drop(k.bitmap);
                }
            }
        }
    }
    // Paper comparison sheet: the four Paper boards side by side at 2x.
    {
        const View views[] = {V(Phase::Available), V(Phase::Downloading, 52848230, 209715200, 14), V(Phase::Ready), V(Phase::Installed)};
        Canvas k; const float W = 14 + 4 * (269 + 14), H = 153.6f + 28 + 16;
        CHECK(k.Create(painter, W, H, 192));
        k.rt->BeginDraw(); Background(painter, W, H, false);
        for (int i = 0; i < 4; ++i) { Presenter pr; Frame f = Settled(pr, painter, views[i], 269); PaintAt(painter, f, 14 + i * 283.f, 14 + f.height + 8, 269, rest, false); }
        CHECK(SUCCEEDED(k.rt->EndDraw())); CHECK(k.Save(L"paper-sheet-192.png"));
    }
    // Interaction states on the primary button: hover, pressed (0.96), keyboard focus.
    {
        Canvas k; const float W = 14 + 3 * 283.f, H = 153.6f + 28;
        CHECK(k.Create(painter, W, H, 192));
        k.rt->BeginDraw(); Background(painter, W, H, false);
        ButtonVisual states[3][2] = {{{1, 0, false, false}, {}}, {{1, 1, false, false}, {}}, {{0, 0, true, true}, {}}};
        for (int i = 0; i < 3; ++i) { Presenter pr; Frame f = Settled(pr, painter, V(Phase::Available), 269); PaintAt(painter, f, 14 + i * 283.f, H - 14, 269, states[i], false); }
        CHECK(SUCCEEDED(k.rt->EndDraw())); CHECK(k.Save(L"button-states-192.png"));
    }
    // Transition strip: Available -> Downloading -> (interrupt) Ready, real presenter frames.
    {
        const double times[] = {0, 40, 80, 120, 160, 220, 300, 420};
        Canvas k; const float W = 14 + 8 * 283.f, H = 153.6f + 40;
        CHECK(k.Create(painter, W, H, 144));
        k.rt->BeginDraw(); Background(painter, W, H, false);
        Presenter pr; pr.SetReduced(false, T0());
        auto a = Describe(V(Phase::Available)); pr.Set(a, painter.Measure(a, 269), T0()); pr.Settle(At(1000));
        auto d = Describe(V(Phase::Downloading, 52848230, 209715200, 14)); pr.Set(d, painter.Measure(d, 269), At(1000));
        for (int i = 0; i < 8; ++i) { Frame f = pr.Sample(At(1000 + times[i])); PaintAt(painter, f, 14 + i * 283.f, H - 14 + f.offset, 269, rest, false); }
        CHECK(SUCCEEDED(k.rt->EndDraw())); CHECK(k.Save(L"transition-available-downloading-144.png"));
    }
    {
        const double times[] = {0, 50, 100, 150, 200, 260, 330, 450};
        Canvas k; const float W = 14 + 8 * 283.f, H = 153.6f + 40;
        CHECK(k.Create(painter, W, H, 144));
        k.rt->BeginDraw(); Background(painter, W, H, false);
        Presenter pr; pr.SetReduced(false, T0());
        auto a = Describe(V(Phase::Available)); pr.Set(a, painter.Measure(a, 269), T0());
        for (int i = 0; i < 8; ++i) { Frame f = pr.Sample(At(times[i])); PaintAt(painter, f, 14 + i * 283.f, H - 14 + f.offset, 269, rest, false); }
        CHECK(SUCCEEDED(k.rt->EndDraw())); CHECK(k.Save(L"entrance-144.png"));
    }
    // Narrow / large-scale layouts: minimum-width card at 200% and 250% scale.
    for (float dpi : {192.f, 240.f}) {
        Presenter pr; Frame f = Settled(pr, painter, V(Phase::Error, 0, 0, -1, L"Le serveur de mises à jour est injoignable."), MinWidth);
        Canvas t; CHECK(t.Create(painter, MinWidth, f.height, dpi));
        t.rt->BeginDraw(); t.rt->Clear(D2D1::ColorF(0, 0, 0, 0)); painter.Paint(f, MinWidth, f.height, rest, false); CHECK(SUCCEEDED(t.rt->EndDraw()));
        auto px = t.Pixels(); const int s = (int)std::lround(dpi / 96 * 3); int outside = 0;
        for (UINT y = 0; y < t.h; ++y) for (UINT x = 0; x < t.w; ++x) if (px[(y * t.w + x) * 4 + 1] > 90 && ((int)x < s || (int)y < s || x >= t.w - s || y >= t.h - s)) ++outside;
        CHECK(outside == 0);
        wchar_t name[64]; std::swprintf(name, 64, L"narrow-error-%d.png", (int)dpi); CHECK(t.Save(name));
    }
}

// ---- Popup window behaviour ----------------------------------------------------------
static LRESULT CALLBACK OwnerProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static void Pump(int ms) {
    const DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((int)(end - GetTickCount()) > 0) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
}

static void WindowTests() {
    WNDCLASSW wc{}; wc.lpfnWndProc = OwnerProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"ToastTestOwner";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); RegisterClassW(&wc);
    const UINT sys = GetDpiForSystem();
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"toast test owner (native, mock)", WS_OVERLAPPEDWINDOW,
                                 40, 40, MulDiv(900, sys, 96), MulDiv(640, sys, 96), nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(owner != nullptr); if (!owner) return;
    ShowWindow(owner, SW_SHOWNORMAL); UpdateWindow(owner); SetForegroundWindow(owner); SetFocus(owner);
    Pump(100);
    HWND foreground = GetForegroundWindow();
    std::vector<Action> actions; int releases = 0;
    Toast toast;
    ToastOptions opt; opt.forceReducedMotion = 0;
    CHECK(toast.Init(owner, repo + L"\\assets", [&](Action a) { actions.push_back(a); }, opt));
    toast.SetFocusReleaseHandler([&] { ++releases; SetActiveWindow(owner); SetFocus(owner); });
    HWND popup = toast.Window();
    CHECK(popup && GetWindow(popup, GW_OWNER) == owner);
    const LONG_PTR ex = GetWindowLongPtrW(popup, GWL_EXSTYLE);
    CHECK((ex & WS_EX_LAYERED) && (ex & WS_EX_NOACTIVATE) && (ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_TOPMOST));
    CHECK(!IsWindowVisible(popup) && !toast.Shown());
    auto* impl = reinterpret_cast<Toast::Impl*>(GetWindowLongPtrW(popup, GWLP_USERDATA));
    const auto statsBefore = impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0;
    toast.SetView(V(Phase::Available));
    Pump(600);
    CHECK(toast.Shown() && IsWindowVisible(popup));
    CHECK(GetForegroundWindow() == foreground && GetFocus() == owner); // never steals focus
    // Bottom-left anchoring against the owner client.
    RECT client{}; GetClientRect(owner, &client); MapWindowPoints(owner, nullptr, (POINT*)&client, 2);
    RECT pr{}; GetWindowRect(popup, &pr);
    const float s = GetDpiForWindow(owner) / 96.f;
    CHECK(pr.left == client.left + (LONG)std::lround(14 * s));
    CHECK(pr.bottom == client.bottom - (LONG)std::lround(14 * s));
    CHECK(pr.right - pr.left == (LONG)std::lround(269 * s));
    CHECK(std::abs((pr.bottom - pr.top) - (LONG)std::lround(Paper::available * s)) <= 1);
    // Idle: zero frame wakes once settled.
    const auto posted = impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0;
    CHECK(posted > statsBefore);
    Pump(400);
    CHECK((impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0) == posted);
    CHECK(!impl->fallbackFrames);
    // Hit testing: card inside = client, rounded corner / outside = transparent.
    auto hit = [&](int x, int y) { return SendMessageW(popup, WM_NCHITTEST, 0, MAKELPARAM(x, y)); };
    CHECK(hit((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2) == HTCLIENT);
    CHECK(hit(pr.left, pr.top) == HTTRANSPARENT && hit(pr.right - 1, pr.bottom - 1) == HTTRANSPARENT);
    CHECK(hit(pr.left - 5, pr.bottom - 20) == HTTRANSPARENT);
    // Click Download (client coordinates of the popup), action delivered after a posted message.
    const Geometry& g = impl->presenter.CurrentGeometry();
    const int bx = (int)std::lround((g.buttons[0].left + g.buttons[0].right) / 2 * s), by = (int)std::lround((g.buttons[0].top + g.buttons[0].bottom) / 2 * s);
    SendMessageW(popup, WM_MOUSEMOVE, 0, MAKELPARAM(bx, by));
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
    CHECK(actions.empty());
    Pump(50);
    CHECK(actions.size() == 1 && actions[0] == Action::Download);
    CHECK(GetForegroundWindow() == foreground && GetFocus() == owner);
    // Phase flips between press and release: Download never becomes Cancel.
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
    toast.SetView(V(Phase::Downloading, 52848230, 209715200, 14));
    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
    Pump(500);
    CHECK(actions.size() == 1);
    GetWindowRect(popup, &pr);
    CHECK(pr.bottom == client.bottom - (LONG)std::lround(14 * s));                       // bottom edge anchored
    CHECK(std::abs((pr.bottom - pr.top) - (LONG)std::lround(Paper::downloading * s)) <= 1);
    // Keyboard: explicit Focus(), Enter activates, Esc returns focus.
    CHECK(toast.Focus() && GetFocus() == popup);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0); Pump(50);
    CHECK(actions.size() == 2 && actions[1] == Action::Cancel);
    toast.SetView(V(Phase::Error, 0, 0, -1, L"Réseau indisponible"));
    Pump(400);
    CHECK(impl->focus == 1); // primary (Retry) focused after the swap
    SendMessageW(popup, WM_KEYDOWN, VK_TAB, 0);
    CHECK(impl->focus == 0);
    SendMessageW(popup, WM_KEYDOWN, VK_SPACE, 0); SendMessageW(popup, WM_KEYUP, VK_SPACE, 0); Pump(50);
    CHECK(actions.size() == 3 && actions[2] == Action::Dismiss);
    SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0); Pump(50);
    CHECK(releases == 1 && GetFocus() == owner);
    // Resize: card follows the bottom-left anchor; a too-small owner hides it (state kept).
    SetWindowPos(owner, nullptr, 0, 0, MulDiv(700, sys, 96), MulDiv(520, sys, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    toast.Layout(); Pump(400);
    GetClientRect(owner, &client); MapWindowPoints(owner, nullptr, (POINT*)&client, 2); GetWindowRect(popup, &pr);
    CHECK(pr.left == client.left + (LONG)std::lround(14 * s) && pr.bottom == client.bottom - (LONG)std::lround(14 * s));
    SetWindowPos(owner, nullptr, 0, 0, MulDiv(240, sys, 96), MulDiv(400, sys, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    toast.Layout(); Pump(100);
    CHECK(!toast.Fits() && !IsWindowVisible(popup));
    SetWindowPos(owner, nullptr, 0, 0, MulDiv(900, sys, 96), MulDiv(640, sys, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    toast.Layout(); Pump(400);
    CHECK(toast.Fits() && IsWindowVisible(popup));
    // Minimise: hidden, no frames.
    ShowWindow(owner, SW_MINIMIZE); toast.SetOwnerVisible(false); Pump(200);
    CHECK(!IsWindowVisible(popup));
    ShowWindow(owner, SW_RESTORE); toast.SetOwnerVisible(true); Pump(400);
    CHECK(IsWindowVisible(popup));
    // Installed: only by the real event; finite dwell then a single Dismiss and exit.
    toast.SetView(V(Phase::Installed));
    Pump(300);
    CHECK(impl->presenter.Current().kind == Kind::Installed && impl->dwell.Active());
    impl->dwell.Start(M::Now() - Timing::installedDwell + Ms(100)); impl->ArmDwell(M::Now()); // shorten the 6 s dwell for the test
    Pump(600);
    CHECK(actions.size() == 4 && actions[3] == Action::Dismiss && !IsWindowVisible(popup));
    // Hidden view: exit; close is idempotent.
    toast.SetView(V(Phase::Available)); Pump(400);
    toast.SetView(V(Phase::Hidden)); Pump(400);
    CHECK(!IsWindowVisible(popup));
    toast.Close(); toast.Close();
    CHECK(!IsWindow(popup));
    DestroyWindow(owner);
    Pump(50);
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    repo = argc > 1 ? argv[1] : L".";
    snapshots = argc > 2 ? argv[2] : repo + L"\\build\\update-toast\\snapshots";
    CreateDirectoryW(snapshots.c_str(), nullptr);
    ModelTests();
    MotionTests();
    ClickAndDwellTests();
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) { std::fprintf(stderr, "WIC unavailable\n"); return 2; }
    {
        Painter painter;
        if (FAILED(painter.Init(repo + L"\\assets"))) { std::fprintf(stderr, "painter init failed\n"); return 2; }
        PainterTests(painter);
        SnapshotTests(painter);
    }
    bool windows = true;
    for (int i = 3; i < argc; ++i) if (std::wstring(argv[i]) == L"--no-window") windows = false;
    if (windows) WindowTests();
    Drop(wic);
    CoUninitialize();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
