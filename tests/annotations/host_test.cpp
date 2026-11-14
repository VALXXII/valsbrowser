// Annotation mode — host logic tests (T3): page message validation, crop planning (100 % / 150 % /
// 200 %), origins and projects, status summary. Pure: no WebView2, no window, no I/O.
#include <cstdio>
#include <string>

#include "annotation-host.h"
#include "annotation-script.h"

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
    BA::Annotation a; a.project = "boutique"; a.target = c.selection.target; BA::Sanitize(a);
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

static void Projects() {
    CHECK(AH::OriginOf(L"http://127.0.0.1:5173/checkout?x=1#a") == "http://127.0.0.1:5173");
    CHECK(AH::OriginOf(L"HTTPS://User:pw@Example.FR:443/a") == "https://example.fr");
    CHECK(AH::OriginOf(L"http://localhost:80/") == "http://localhost");
    CHECK(AH::OriginOf(L"file:///C:/x.html").empty() && AH::OriginOf(L"about:blank").empty());
    CHECK(AH::LabelFor("http://127.0.0.1:5173", L"Paiement \u2014 Boutique") == "Boutique");
    CHECK(AH::LabelFor("http://127.0.0.1:5173", L"Sans séparateur") == "127.0.0.1:5173");
    std::vector<BA::Project> existing{{"boutique", "Boutique", {"http://localhost:5173"}}};
    auto p = AH::NewProjectFor("http://127.0.0.1:5173", L"Paiement \u2014 Boutique", existing);
    CHECK(p.id == "boutique-2" && p.label == "Boutique" && p.origins.size() == 1 && BA::IsValidProjectId(p.id));
    auto h = AH::NewProjectFor("http://127.0.0.1:5173", L"x", existing, true);
    CHECK(h.id == "127-0-0-1-5173" && BA::IsValidProjectId(h.id));
    CHECK(AH::ProjectForOrigin(existing, "http://localhost:5173") == 0 && AH::ProjectForOrigin(existing, "http://localhost:5174") == -1);
    CHECK(AH::SlugFor("CON") == "con" || !BA::IsValidProjectId("con"));
    CHECK(BA::IsValidProjectId(AH::SlugFor("CON")) && BA::IsValidProjectId(AH::SlugFor("Été 2026 !")));
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

int main() {
    Envelopes(); Crops(); Projects(); Status(); Script();
    std::printf("host_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
