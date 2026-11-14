// Annotation mode — host logic tests (T3): page message validation, crop planning (100 % / 150 % /
// 200 %), origins and projects, status summary. Pure: no WebView2, no window, no I/O.
#include <cstdio>
#include <string>

#include "annotation-host.h"
#include "annotation-script.h"
#include "annotation-capture.h"

static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

namespace AH = AnnotationHost;
namespace BA = BrowserAnnotations;

static std::string Message(const std::string& nonce, long long stamp, const std::string& op, const std::string& extra = "") {
    return "{\"op\":\"" + op + "\"" + extra + ",\"type\":\"browser-annotation\",\"v\":1,\"nonce\":\"" + nonce + "\",\"navStamp\":" + std::to_string(stamp) + "}";
}
static std::string Select(const std::string& target, const std::string& frame = "main") {
    return ",\"keyboard\":false,\"frame\":{\"kind\":\"" + frame + "\"},\"target\":" + target +
           ",\"viewport\":{\"width\":1000,\"height\":800,\"scrollX\":0,\"scrollY\":420,\"devicePixelRatio\":1.5}";
}
static const std::string GoodTarget =
    "{\"kind\":\"element\",\"selector\":\"main form.checkout > button.btn-primary\",\"selectorUnique\":true,"
    "\"path\":[\"html\",\"body\",\"main\",\"form.checkout\",\"button.btn-primary\"],\"shadowPath\":[],\"tag\":\"button\",\"role\":\"button\","
    "\"accessibleName\":\"Payer\",\"attributes\":{\"id\":null,\"class\":\"btn-primary\",\"value\":\"hunter2\",\"onclick\":\"x()\"},"
    "\"text\":\"Payer 48,00 \\u20ac\",\"selectionText\":null,\"rect\":{\"x\":412,\"y\":688,\"width\":120,\"height\":40},"
    "\"styles\":{\"display\":\"inline-flex\",\"color\":\"rgb(255, 255, 255)\",\"--secret\":\"1\"},\"sourceAttributes\":{}}";

static void Envelopes() {
    const std::string n = "clm_abcdefghijklmnopqrstuvwxyz";
    auto c = AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget)), n, 7);
    CHECK(c.env.op == AH::Op::Select);
    CHECK(c.selection.target.tag == "button" && c.selection.target.selectorUnique);
    CHECK(c.selection.target.viewport.devicePixelRatio == 1.5);
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget)), n + "x", 7).env.op == AH::Op::Invalid);   // nonce
    CHECK(AH::CheckMessage(Message(n, 8, "select", Select(GoodTarget)), n, 7).env.op == AH::Op::Invalid);        // navStamp
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget)), "", 7).env.op == AH::Op::Invalid);        // no session
    CHECK(AH::CheckMessage(Message(n, 7, "navigate"), n, 7).env.op == AH::Op::Invalid);                           // unknown op
    CHECK(AH::CheckMessage(Message(n, 7, "escape"), n, 7).env.op == AH::Op::Escape);
    auto h = AH::CheckMessage(Message(n, 7, "hidden", ",\"serial\":12"), n, 7);
    CHECK(h.env.op == AH::Op::Hidden && h.env.serial == 12);
    CHECK(AH::CheckMessage("{\"type\":\"browser-annotation\",\"v\":2,\"nonce\":\"" + n + "\",\"navStamp\":7,\"op\":\"escape\"}", n, 7).env.op == AH::Op::Invalid);
    CHECK(AH::CheckMessage("{\"type\":\"browser-annotation\",\"type\":\"x\"}", n, 7).env.op == AH::Op::Invalid);   // duplicate key
    CHECK(AH::CheckMessage("not json", n, 7).env.op == AH::Op::Invalid);
    // Size: one message <= 64 KiB.
    std::string big = Message(n, 7, "escape", ",\"pad\":\"" + std::string(70000, 'a') + "\"");
    CHECK(AH::CheckMessage(big, n, 7).env.op == AH::Op::Invalid);
    // Schema: wrong types and inconsistent kinds are refused.
    std::string t = GoodTarget; t.replace(t.find("\"selectorUnique\":true"), 21, "\"selectorUnique\":\"yes\"");
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(t)), n, 7).env.op == AH::Op::Invalid);
    t = GoodTarget; t.replace(t.find("\"kind\":\"element\""), 16, "\"kind\":\"selection\"");
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(t)), n, 7).env.op == AH::Op::Invalid);                  // selection without text
    t = GoodTarget; t.replace(t.find("\"width\":120"), 11, "\"width\":-5");
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(t)), n, 7).env.op == AH::Op::Invalid);
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget, "elsewhere")), n, 7).env.op == AH::Op::Invalid);
    t = GoodTarget; t.replace(t.find("\"selectionText\":null"), 20, "\"selectionText\":[1]");
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(t)), n, 7).env.op == AH::Op::Invalid);
    // A hostile page's extra attributes/styles are dropped by the store's allow-lists.
    BA::Annotation a; a.target = c.selection.target; BA::Sanitize(a);
    bool value = false, onclick = false, custom = false;
    for (const auto& v : a.target.attributes) { value |= v.name == "value"; onclick |= v.name == "onclick"; }
    for (const auto& v : a.target.styles) custom |= v.name == "--secret";
    CHECK(!value && !onclick && !custom);
    CHECK(AH::LooksLikeAnnotation(L"{\"op\":\"x\",\"type\":\"browser-annotation\"}") && !AH::LooksLikeAnnotation(L"\"scroll:1\""));
}

static void Crops() {
    BA::Rect r{100, 100, 200, 50};
    auto p = AH::PlanCrop(r, 1000, 800, 1000, 800);   // 100 %
    CHECK(p.ok && p.x == 76 && p.y == 76 && p.w == 248 && p.h == 98 && p.outW == 248 && p.scale == 1);
    CHECK(p.markX == 24 && p.markY == 24 && p.markW == 200 && p.markH == 50 && p.crop == "element+margin");
    auto q = AH::PlanCrop(r, 1000, 800, 1500, 1200);  // 150 %
    CHECK(q.ok && q.x == 114 && q.y == 114 && q.w == 372 && q.h == 147 && q.markX == 36 && q.markW == 300);
    auto z = AH::PlanCrop(r, 1000, 800, 2000, 1600);  // 200 %
    CHECK(z.ok && z.x == 152 && z.w == 496 && z.markW == 400);
    auto big = AH::PlanCrop({0, 0, 3000, 2000}, 3000, 2000, 6000, 4000);   // larger than 1600: reduced
    CHECK(big.ok && big.outW == 1600 && big.outH <= 1600 && big.scale < .27 && big.crop == "element+margin (viewport edge)");
    auto edge = AH::PlanCrop({-50, 700, 200, 300}, 1000, 800, 1000, 800);   // partly off screen
    CHECK(edge.ok && edge.x == 0 && edge.y == 676 && edge.h == 124 && edge.crop == "element+margin (visible part)");
    CHECK(!AH::PlanCrop({2000, 2000, 10, 10}, 1000, 800, 1000, 800).ok);   // invisible
    CHECK(!AH::PlanCrop(r, 0, 800, 1000, 800).ok);
    auto stale = AH::PlanCrop(r, 1000, 834, 1250, 960);   // viewport height measured before the 32 DIP band: uniform scale
    CHECK(stale.ok && stale.y == 95 && stale.h == 123 && stale.markY == 30);
}

// V2: the engine clip (Page.captureScreenshot) — document coordinates, viewport limits, <= 1600 px.
static void Clips() {
    BA::Viewport vp; vp.width = 1000; vp.height = 800; vp.scrollX = 0; vp.scrollY = 420; vp.devicePixelRatio = 1.25;
    auto p = AH::PlanClip({100, 100, 200, 50}, vp);
    CHECK(p.ok && p.x == 76 && p.y == 496 && p.width == 248 && p.height == 98 && p.scale == 1);   // scroll added, margin 24
    CHECK(p.outW == 310 && p.outH == 123 && p.markX == 24 && p.markY == 24 && p.markW == 200 && p.markH == 50 && p.crop == "element+margin");
    vp.devicePixelRatio = 1.875;   // 150 % zoom at 125 %
    auto z = AH::PlanClip({100.4, 100.6, 200, 50}, vp);
    CHECK(z.ok && z.x == 76 && z.width == 249 && z.outW == unsigned(std::lround(249 * 1.875)) && z.markX > 24 && z.markX < 25);
    auto big = AH::PlanClip({0, 0, 3000, 2000}, BA::Viewport{3000, 2000, 0, 0, 2, 1});
    CHECK(big.ok && big.scale < .27 && big.outW == 1600 && big.outH <= 1600 && big.crop == "element+margin (viewport edge)");
    auto edge = AH::PlanClip({-50, 700, 200, 300}, BA::Viewport{1000, 800, 0, 0, 1, 1});
    CHECK(edge.ok && edge.x == 0 && edge.y == 676 && edge.height == 124 && edge.crop == "element+margin (visible part)");
    CHECK(!AH::PlanClip({2000, 2000, 10, 10}, BA::Viewport{1000, 800, 0, 0, 1, 1}).ok);   // invisible
    CHECK(!AH::PlanClip({1, 1, 10, 10}, BA::Viewport{0, 800, 0, 0, 1, 1}).ok);
    const std::string params = AH::ClipParams(p);
    CHECK(params.find("\"clip\":{\"x\":76,\"y\":496,\"width\":248,\"height\":98,\"scale\":1}") != std::string::npos || params.find("\"x\":76") != std::string::npos);
    CHECK(params.find("\"format\":\"png\"") != std::string::npos && params.find("\"captureBeyondViewport\":false") != std::string::npos);
    // Reply decoding: strict JSON + base64 + PNG signature.
    std::string png;
    CHECK(AH::ScreenshotData("{\"data\":\"iVBORw0KGgoAAAANSUhEUg==\"}", png) && png.size() == 16 && png.compare(0, 4, "\x89PNG") == 0);
    CHECK(!AH::ScreenshotData("{\"data\":\"aGVsbG8gd29ybGQh\"}", png));   // not a PNG
    CHECK(!AH::ScreenshotData("{\"data\":\"iVBOR*0KGgo=\"}", png));        // bad base64
    CHECK(!AH::ScreenshotData("{\"error\":1}", png) && !AH::ScreenshotData("[]", png) && !AH::ScreenshotData("{\"data\":\"\"}", png));
    std::string out;
    CHECK(AH::Base64Decode("TWFu", out) && out == "Man" && AH::Base64Decode("TWE=", out) && out == "Ma" && AH::Base64Decode("TQ==", out) && out == "M");
    CHECK(!AH::Base64Decode("TQ=a", out) && !AH::Base64Decode("TWF", out));
    // The selection counter and the click time travel with « select ».
    const std::string n = "clm_abcdefghijklmnopqrstuvwxyz";
    auto c = AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget) + ",\"seq\":3,\"at\":1763112000000"), n, 7);
    CHECK(c.env.op == AH::Op::Select && c.selection.seq == 3 && c.selection.at == 1763112000000.0);
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget) + ",\"seq\":-1"), n, 7).env.op == AH::Op::Invalid);
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget) + ",\"seq\":\"3\""), n, 7).env.op == AH::Op::Invalid);
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget)), n, 7).selection.seq == -1);
}

static void Texts() {
    BA::Target t; t.tag = "button"; t.role = "button"; t.accessibleName = "Payer 48,00 \xE2\x82\xAC";
    CHECK(AH::ElementLabel(t) == L"Bouton « Payer 48,00 € »");
    t.tag = "a"; t.role = "link"; t.accessibleName = ""; t.text = "  Mentions\nlégales ";
    CHECK(AH::ElementLabel(t) == L"Lien « Mentions légales »");
    t.tag = "div"; t.role = ""; t.text = "";
    CHECK(AH::ElementLabel(t) == L"Élément div");
    t.kind = "selection"; t.selectionText = std::string(60, 'x');
    CHECK(AH::ElementLabel(t).rfind(L"Texte sélectionné « ", 0) == 0 && AH::ElementLabel(t).find(L"…") != std::wstring::npos);
    CHECK(AH::AgeLabel(30, 0, 1, 14, 11) == L"À l'instant" && AH::AgeLabel(125, 0, 1, 14, 11) == L"2 min" && AH::AgeLabel(7300, 0, 1, 14, 11) == L"2 h");
    CHECK(AH::AgeLabel(30000, 1, 5, 13, 11) == L"Hier" && AH::AgeLabel(300000, 3, 2, 11, 11) == L"Mardi" && AH::AgeLabel(900000, 10, 2, 4, 11) == L"4 nov.");
    CHECK(AH::AgentDisplay("Claude Desktop") == L"Claude" && AH::AgentDisplay("codex-mcp-client") == L"Codex" && AH::AgentDisplay("try-mcp (sandbox)") == L"try-mcp (sandbox)" && AH::AgentDisplay("") == L"Un agent");
    CHECK(AH::AgentKindOf("claude-ai") == 0 && AH::AgentKindOf("Codex") == 1 && AH::AgentKindOf("x") == 2);
    CHECK(AH::SinceLower(130) == L"depuis 2 min" && AH::SinceLower(10) == L"à l'instant");
}

static void Origins() {
    CHECK(AH::OriginOf(L"http://127.0.0.1:5173/checkout?x=1#a") == "http://127.0.0.1:5173");
    CHECK(AH::OriginOf(L"HTTPS://User:pw@Example.FR:443/a") == "https://example.fr");
    CHECK(AH::OriginOf(L"http://localhost:80/") == "http://localhost");
    CHECK(AH::OriginOf(L"file:///C:/x.html").empty() && AH::OriginOf(L"about:blank").empty());
    for (const char* u : {"http://127.0.0.1:5173/a?b", "HTTPS://User:pw@Example.FR:443/a", "http://localhost:80/"})
        CHECK(AH::OriginOf(AH::Wide(u)) == BA::SiteOrigin(u));   // the host and the store agree on the site key
}
static void Status() {
    std::vector<BA::Entry> e(3);
    e[0].annotation.id = "a"; e[0].state.status = BA::Status::Claimed; e[0].state.events = {{100, "agent", "open", "claimed", ""}};
    e[1].annotation.id = "b"; e[1].state.status = BA::Status::Claimed; e[1].state.events = {{200, "agent", "open", "claimed", ""}};
    e[2].annotation.id = "c"; e[2].state.status = BA::Status::Resolved; e[2].state.resolution = BA::Resolution{"resolved", "Corrigé", "claude", 300};
    auto s = AH::Summarize(e, 0);
    CHECK(s.claimed == 2 && s.working && s.working->annotation.id == "b" && s.resolved && s.resolved->annotation.id == "c");
    CHECK(!AH::Summarize(e, 301).resolved);
    CHECK(AH::OutcomeHeadline(e[2]) == L"Résolue par");
    CHECK(AH::SinceText(30) == L"À l'instant" && AH::SinceText(130) == L"Depuis 2 min");
    CHECK(AH::StoreTag(L"C:\\A\\b") == AH::StoreTag(L"c:\\a\\B") && AH::StoreTag(L"x").size() == 6);
}

static void Script() {
    const std::wstring js = NativeChrome::Annotations::PageScript();
    CHECK(js.find(L"attachShadow({ mode: 'closed' })") != std::wstring::npos);
    CHECK(js.find(L"requestAnimationFrame(frame)") != std::wstring::npos);    // hit-test at most once per frame
    CHECK(js.find(L"prefers-reduced-motion") != std::wstring::npos);
    CHECK(js.find(L"120ms") != std::wstring::npos && js.find(L"transition:") != std::wstring::npos);
    CHECK(js.find(L".value") == std::wstring::npos);                           // never reads a field value
    CHECK(js.find(L"localStorage") == std::wstring::npos && js.find(L"document.cookie") == std::wstring::npos && js.find(L"outerHTML") == std::wstring::npos);
    CHECK(js.find(L"removeEventListener") != std::wstring::npos && js.find(L"host.remove()") != std::wstring::npos);
    const auto cmd = NativeChrome::Annotations::PageCommand(L"clm_x", L"hide", L"3");
    CHECK(cmd.find(L"h.nonce==='clm_x'") != std::wstring::npos && cmd.find(L"h.cmd('hide',3)") != std::wstring::npos);
}

// V3-A: capture without any visible change — the page's geometry (« mark »), « shown », and the fill removed
// from the image arithmetically (exact up to rounding, at fractional edges too).
static void V3Capture() {
    const std::string n = "clm_abcdefghijklmnopqrstuvwxyz";
    const std::string mark = ",\"seq\":3,\"mark\":{\"x\":411.5,\"y\":688.25,\"w\":120,\"h\":40,\"lx\":411.5,\"ly\":666.25,\"lw\":152.4,\"lh\":20,\"text\":\"button.btn-primary \u00b7 120\u00d740\",\"fill\":0.08}";
    auto c = AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget) + mark), n, 7);
    CHECK(c.env.op == AH::Op::Select && c.selection.mark.present && c.selection.mark.x == 411.5 && c.selection.mark.lw == 152.4 && c.selection.mark.fill == 0.08 && c.selection.seq == 3);
    CHECK(c.selection.mark.text == "button.btn-primary \xc2\xb7 120\xc3\x97" "40");
    auto noMark = AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget)), n, 7); CHECK(noMark.env.op == AH::Op::Select && !noMark.selection.mark.present);
    std::string bad = mark; bad.replace(bad.find("\"fill\":0.08"), 11, "\"fill\":3.00");
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget) + bad), n, 7).env.op == AH::Op::Invalid);
    bad = mark; bad.replace(bad.find("\"w\":120"), 7, "\"w\":\"1\"");
    CHECK(AH::CheckMessage(Message(n, 7, "select", Select(GoodTarget) + bad), n, 7).env.op == AH::Op::Invalid);
    auto shown = AH::CheckMessage(Message(n, 7, "shown", ",\"serial\":42"), n, 7); CHECK(shown.env.op == AH::Op::Shown && shown.env.serial == 42);
    // Plan: the clip's viewport origin is kept for the fill mapping.
    BA::Rect r; r.x = 411.5; r.y = 688.25; r.width = 120; r.height = 40; BA::Viewport vp; vp.width = 1000; vp.height = 800; vp.scrollY = 420; vp.devicePixelRatio = 1.5;
    const auto plan = AH::PlanClip(r, vp); CHECK(plan.ok && plan.viewX == 387 && plan.viewY == 664 && plan.y == 664 + 420);
    const auto f = AnnotationCapture::FillIn(plan, &c.selection.mark, plan.outW, plan.outH, 62, 103, 255);
    // device px: origin = floor(387 x 1.5) = 580 (the engine starts the shot on a whole pixel), scroll 420 x 1.5 whole
    CHECK(f.on && std::fabs(f.x0 - (411.5 * 1.5 - 580) * (plan.outW / plan.width / 1.5)) < 1e-9 && std::fabs(f.y1 - ((688.25 + 40) * 1.5 - 996) * (plan.outH / plan.height / 1.5) * (plan.outW / plan.width) / (plan.outH / plan.height)) < 1e-6);
    {   // a fractional scroll (0.2 CSS px at 1.25): the content sits a quarter pixel lower than getBoundingClientRect says
        BA::Viewport v2 = vp; v2.scrollY = 432.2; v2.devicePixelRatio = 1.25; const auto p2 = AH::PlanClip(r, v2);
        const auto f2 = AnnotationCapture::FillIn(p2, &c.selection.mark, p2.outW, p2.outH, 62, 103, 255);
        const double origin = std::floor(p2.viewY * 1.25 + .25 + 1e-6);
        CHECK(std::fabs(f2.y0 - (688.25 * 1.25 + .25 - origin) * (p2.outW / p2.width / 1.25)) < 1e-6);
    }
    // Removal: a synthetic page under an 8 % accent fill, with fractional edges, comes back within 1 level.
    const unsigned w = 64, h = 40; std::vector<std::uint8_t> page(size_t(w) * h * 4), shot;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) { auto* p = &page[(size_t(y) * w + x) * 4]; p[0] = std::uint8_t((x * 37 + y * 11) & 255); p[1] = std::uint8_t((x * 5 + 200) & 255); p[2] = std::uint8_t((y * 9 + x) & 255); p[3] = 255; }
    AnnotationCapture::Fill fill; fill.on = true; fill.x0 = 10.4; fill.x1 = 50.75; fill.y0 = 6.5; fill.y1 = 33.2; fill.r = 62; fill.g = 103; fill.b = 255; fill.alpha = .08;
    shot = page;   // what the engine renders: page * (1 - a) + accent * a, rounded (a = 0.08 x coverage)
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const double a = fill.alpha * AnnotationCapture::Coverage(y, y + 1, fill.y0, fill.y1) * AnnotationCapture::Coverage(x, x + 1, fill.x0, fill.x1); if (a <= 0) continue;
        auto* p = &shot[(size_t(y) * w + x) * 4]; const double acc[3] = {fill.b, fill.g, fill.r};
        for (int ch = 0; ch < 3; ++ch) p[ch] = std::uint8_t(std::lround(p[ch] * (1 - a) + acc[ch] * a));
    }
    AnnotationCapture::RemoveFill(shot.data(), w, h, w * 4, fill);
    int worst = 0; for (size_t i = 0; i < page.size(); ++i) worst = std::max(worst, std::abs(int(page[i]) - int(shot[i])));
    std::printf("fill removal: worst channel error %d level(s) over %u x %u\n", worst, w, h);
    CHECK(worst <= 1);
    CHECK(AnnotationCapture::Unblend(200, 255, 0) == 200 && AnnotationCapture::Unblend(int(std::lround(100 * .92 + 255 * .08)), 255, .08) == 100);
    // The script hands the edges and label over, keeps the fill, and answers « shown » only when asked with a serial.
    const std::wstring js = NativeChrome::Annotations::PageScript();
    CHECK(js.find(L"op === 'handoff'") != std::wstring::npos && js.find(L"conceal(true, true)") != std::wstring::npos && js.find(L"op: 'shown'") != std::wstring::npos);
    CHECK(js.find(L"m.mark = geometry(info)") != std::wstring::npos && js.find(L"conceal(true); ") == std::wstring::npos);   // no hiding at the click any more
}

int main() {
    Envelopes(); Crops(); Clips(); Origins(); Status(); Texts(); Script(); V3Capture();
    std::printf("host_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
