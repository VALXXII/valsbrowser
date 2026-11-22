// Comment mode v4, H2 (R26, R27, R31, R33, R36, R37): the pure host parts, and the scripts the headless page test runs.
// Usage: page-v4-test.exe <outDir>   — writes <outDir>/page-script.js (the page script) and <outDir>/swap.js (R36 level 2,
// for the files listed in the test page). No window, no WebView2: tests/annotations-ui/page_v4.py drives the page part.
#include "../../src/annotation-host.h"
#include "../../src/annotation-script.h"
#include "../../src/chat-transition.h"
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static bool Near(double a, double b, double e) { return std::fabs(a - b) <= e; }
// The native frame's edge (annotation-frame.h, EdgeCss) — copied here to keep this test free of Direct2D.
static double EdgeCss(double dpr) { return std::max(1.0, std::round(2 * dpr)) / dpr; }

static void Peek() {
    CHECK(AnnotationHost::PeekArg("section.hero", true) == "{\"active\":true,\"selector\":\"section.hero\"}");
    CHECK(AnnotationHost::PeekArg("section.hero", false) == "{\"active\":false}");
    CHECK(AnnotationHost::PeekArg("", true) == "{\"active\":false}");
    CHECK(AnnotationHost::PeekArg("a[data-x=\"1\"]", true) == "{\"active\":true,\"selector\":\"a[data-x=\\\"1\\\"]\"}");
    const std::wstring js = NativeChrome::Annotations::PageScript();
    CHECK(js.find(L"if (op === 'peek') return peekSet(arg);") != std::wstring::npos);
    CHECK(js.find(L"behavior: reduced ? 'auto' : 'smooth'") != std::wstring::npos);   // R26: soft scroll, none when reduced
}
static void Anchor() {   // R39: optional sx, sy, t on « anchor »
    namespace AH = AnnotationHost;
    const std::string head = "{\"type\":\"browser-annotation\",\"v\":1,\"nonce\":\"n4\",\"navStamp\":1,\"op\":\"anchor\",\"x\":1,\"y\":2,\"w\":3,\"h\":4,\"vw\":800,\"vh\":600";
    auto c = AH::CheckMessage(head + ",\"sx\":0,\"sy\":1234.5,\"t\":1763650000123.25}", "n4", 1);
    CHECK(c.env.op == AH::Op::Anchor && c.env.sy == 1234.5 && c.env.t == 1763650000123.25 && c.env.y == 2);
    CHECK(AH::CheckMessage(head + "}", "n4", 1).env.op == AH::Op::Anchor);                        // still optional
    CHECK(AH::CheckMessage(head + ",\"sy\":\"x\"}", "n4", 1).env.op == AH::Op::Invalid);
    const std::wstring js = NativeChrome::Annotations::PageScript();
    CHECK(js.find(L"function onScroll() { if (follow) followPost();") != std::wstring::npos);
    const auto post = js.find(L"var f = followPost();   // posted first"), draw = js.find(L"bxPlace(f ? f.el : null, f ? f.r : null);");
    CHECK(post != std::wstring::npos && draw != std::wstring::npos && post < draw);
}
static void Selecting() {   // latency: the light message at the click, before the full « select »
    namespace AH = AnnotationHost;
    auto check = [](const std::string& j) { return AH::CheckMessage(j, "n4", 1); };
    const std::string head = "{\"type\":\"browser-annotation\",\"v\":1,\"nonce\":\"n4\",\"navStamp\":1,";
    auto c = check(head + "\"op\":\"selecting\",\"seq\":7,\"at\":1763650000000.5,\"selector\":\"section.hero\",\"rect\":{\"x\":10.5,\"y\":-20,\"width\":300,\"height\":40}}");
    {   auto v = check(head + "\"op\":\"selecting\",\"seq\":8,\"selector\":\"a\",\"rect\":{\"x\":0,\"y\":0,\"width\":1,\"height\":1},\"vw\":1200,\"vh\":700}");
        CHECK(v.env.op == AH::Op::Selecting && v.env.vw == 1200 && v.env.vh == 700); }   // the viewport of the click's rectangle
    CHECK(c.env.op == AH::Op::Selecting && c.env.serial == 7 && c.env.selector == "section.hero" && c.env.x == 10.5 && c.env.y == -20 && c.env.w == 300 && c.env.h == 40 && c.env.at > 1.7e12);
    CHECK(check(head + "\"op\":\"selecting\",\"seq\":7,\"selector\":\"\",\"rect\":{\"x\":0,\"y\":0,\"width\":1,\"height\":1}}").env.op == AH::Op::Invalid);
    CHECK(check(head + "\"op\":\"selecting\",\"seq\":7,\"selector\":\"a\",\"rect\":{\"x\":0,\"y\":0,\"width\":-1,\"height\":1}}").env.op == AH::Op::Invalid);
    CHECK(check(head + "\"op\":\"selecting\",\"selector\":\"a\",\"rect\":{\"x\":0,\"y\":0,\"width\":1,\"height\":1}}").env.op == AH::Op::Invalid);
    const std::wstring js = NativeChrome::Annotations::PageScript();
    const auto light = js.find(L"post({ op: 'selecting'"), full = js.find(L"var m = describe(info, selText);");
    CHECK(light != std::wstring::npos && full != std::wstring::npos && light < full);   // posted before the description is computed
}
static void Edges() {   // R27: whole device pixels at 100 / 125 / 150 / 200 %
    CHECK(Near(EdgeCss(1), 2, 1e-9) && Near(EdgeCss(1.25) * 1.25, 3, 1e-9) && Near(EdgeCss(1.5) * 1.5, 3, 1e-9) && Near(EdgeCss(2) * 2, 4, 1e-9));
    const std::wstring js = NativeChrome::Annotations::PageScript();
    CHECK(js.find(L"Math.max(1, Math.round(2 * d)) / d") != std::wstring::npos);   // the same rule in the page
    CHECK(js.find(L"boxTo(m.frame, fb, false)") != std::wstring::npos && js.find(L"boxTo(bx.frame, fb)") != std::wstring::npos && js.find(L"boxTo(fill, fb)") != std::wstring::npos);
}
static void Ctrl() {   // R31: Ctrl never consumed, shortcuts left alone
    const std::wstring js = NativeChrome::Annotations::PageScript();
    CHECK(js.find(L"if (e.isTrusted && e.key === 'Control') { setCtrl(true); return; }") != std::wstring::npos);
    CHECK(js.find(L"on(window, 'keyup', onKeyUp, true);") != std::wstring::npos);
}
// Review M3: the caller hears how the transition ended (a glide too: its « done » mark), once; no WebView2 (stand-ins).
static void TransitionDone() {
    using namespace ChatTransition;
    auto pump = [](const std::function<bool()>& until, int ms) {
        const DWORD end = GetTickCount() + DWORD(ms); MSG m;
        while (!until() && GetTickCount() < end) { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(5); }
    };
    struct Run { std::vector<Outcome> outcomes; int reloads = 0; std::vector<std::wstring> states; };
    auto exec = [](Run* r) {
        return [r](const std::wstring& script, std::function<void(HRESULT, const std::wstring&)> answer) {
            const bool swap = script.find(L"browser-restyle") != std::wstring::npos && script.find(L"__browserRestyle;return") == std::wstring::npos;
            std::wstring a;
            if (swap) a = r->states.empty() ? L"\"started\"" : r->states.front();
            else { a = r->states.size() > 1 ? r->states[1] : L"\"done\""; if (r->states.size() > 2) r->states.erase(r->states.begin() + 1); }
            answer(S_OK, a); return true;
        };
    };
    {   Run r; r.states = {L"\"started\"", L"\"pending\"", L"\"done\""};   // glide: started, still pending once, done
        Runner::Get().ReloadForTest({"styles/site.css"}, false, exec(&r), [&] { ++r.reloads; }, [&](Outcome o) { r.outcomes.push_back(o); });
        pump([&] { return !r.outcomes.empty(); }, 3000);
        CHECK(r.outcomes.size() == 1 && r.outcomes[0] == Outcome::Glided && r.reloads == 0);
        CHECK(!Runner::Get().Busy());
    }
    {   Run r; r.states = {L"\"nomatch\""};   // not a linked sheet: the reload, then « Reloaded »
        Runner::Get().ReloadForTest({"styles/site.css"}, false, exec(&r), [&] { ++r.reloads; }, [&](Outcome o) { r.outcomes.push_back(o); });
        pump([&] { return !r.outcomes.empty(); }, 3000);
        CHECK(r.outcomes.size() == 1 && r.outcomes[0] == Outcome::Reloaded && r.reloads == 1);
    }
    {   Run r; r.states = {L"\"started\"", L"\"failed\""};   // the swap failed in the page: fallback reload
        Runner::Get().ReloadForTest({"styles/site.css"}, false, exec(&r), [&] { ++r.reloads; }, [&](Outcome o) { r.outcomes.push_back(o); });
        pump([&] { return !r.outcomes.empty(); }, 3000);
        CHECK(r.outcomes.size() == 1 && r.outcomes[0] == Outcome::Reloaded && r.reloads == 1);
    }
    {   Run r; r.states = {L"\"started\"", L"\"pending\"", L"\"pending\""};   // a newer transition replaces a pending glide
        Runner::Get().ReloadForTest({"styles/site.css"}, false, exec(&r), [&] { ++r.reloads; }, [&](Outcome o) { r.outcomes.push_back(o); });
        Run r2; Runner::Get().ReloadForTest({"index.html"}, false, exec(&r2), [&] { ++r2.reloads; }, [&](Outcome o) { r2.outcomes.push_back(o); });
        pump([&] { return !r2.outcomes.empty(); }, 3000);
        CHECK(r.outcomes.size() == 1 && r.outcomes[0] == Outcome::Cancelled && r.reloads == 0);
        CHECK(r2.outcomes.size() == 1 && r2.outcomes[0] == Outcome::Reloaded && r2.reloads == 1);
    }
    {   Run r;   // reduced motion: never a glide, the reload
        Runner::Get().ReloadForTest({"styles/site.css"}, true, exec(&r), [&] { ++r.reloads; }, [&](Outcome o) { r.outcomes.push_back(o); });
        pump([&] { return !r.outcomes.empty(); }, 3000);
        CHECK(r.outcomes.size() == 1 && r.outcomes[0] == Outcome::Reloaded && r.reloads == 1);
    }
}
static void Mods() {   // R31: « mods » = the host reads Ctrl (no hook)
    namespace AH = AnnotationHost;
    CHECK(AH::CheckMessage("{\"type\":\"browser-annotation\",\"v\":1,\"nonce\":\"n4\",\"navStamp\":1,\"op\":\"mods\"}", "n4", 1).env.op == AH::Op::Mods);
}
static void Escape() {   // review M4 (b): Escape typed in the page with a bubble open closes the bubble, not the mode
    namespace AH = AnnotationHost;
    CHECK(AH::OnEscape(false, true) == AH::EscapeAction::CloseBubble && AH::OnEscape(true, true) == AH::EscapeAction::CloseBubble);
    CHECK(AH::OnEscape(false, false) == AH::EscapeAction::Leave && AH::OnEscape(true, false) == AH::EscapeAction::Ignore);
}
static void Transition() {   // R36
    using namespace ChatTransition;
    CHECK(StyleOnly({"styles/landing.css"}) && StyleOnly({"a.CSS", "b/c.css"}));
    CHECK(!StyleOnly({}) && !StyleOnly({"styles/landing.css", "index.html"}) && !StyleOnly({"app.scss"}) && !StyleOnly({"css"}));
    CHECK(Near(Bezier(.4, 0, .2, 1, 0), 0, 1e-9) && Near(Bezier(.4, 0, .2, 1, 1), 1, 1e-9) && Near(Bezier(.4, 0, .2, 1, .5), .77556, 1e-4));
    CHECK(Near(Bezier(.23, 1, .32, 1, .25), .77538, 1e-4));
    CHECK(Near(FadeOpacity(0, false), 1, 1e-9) && Near(FadeOpacity(FadeMs, false), 0, 1e-9) && FadeOpacity(-5, false) == 1);
    double prev = 1; bool down = true; for (int ms = 0; ms <= 260; ms += 4) { const double o = FadeOpacity(ms, false); if (o > prev + 1e-12) down = false; prev = o; }
    CHECK(down);
    CHECK(Near(FadeOpacity(75, true), .5, 1e-9) && Near(FadeOpacity(ReducedFadeMs, true), 0, 1e-9));
    CHECK(FadeMs == 250 && GlideMs == 250 && ReducedFadeMs < FadeMs);
    const std::wstring js = SwapScript({"styles/landing.css", "x\"<y>.css", "é.css"}, GlideMs);
    CHECK(js.find(L"\"styles/landing.css\"") != std::wstring::npos && js.find(L"\"x\\u0022\\u003cy\\u003e.css\"") != std::wstring::npos && js.find(L"\"\\u00e9.css\"") != std::wstring::npos);
    CHECK(js.find(L",250)") != std::wstring::npos && js.find(L"cubic-bezier(.23,1,.32,1)") != std::wstring::npos);
    CHECK(Unquote(L"\"started\"") == L"started" && Unquote(nullptr).empty());
}

int wmain(int argc, wchar_t** argv) {
    Peek(); Selecting(); Anchor(); Edges(); TransitionDone(); Mods(); Escape(); Ctrl(); Transition();
    if (argc > 1) {
        const std::wstring out = argv[1];
        auto write = [&](const wchar_t* name, const std::wstring& text) {
            FILE* f = _wfopen((out + L"\\" + name).c_str(), L"wb"); if (!f) { ++failures; return; }
            const std::string u = AnnotationHost::Utf8(text); fwrite(u.data(), 1, u.size(), f); fclose(f);
        };
        write(L"page-script.js", NativeChrome::Annotations::PageScript());
        write(L"swap.js", ChatTransition::SwapScript({"styles/site.css"}, ChatTransition::GlideMs));
        write(L"swap-missing.js", ChatTransition::SwapScript({"styles/site.css", "styles/other.css"}, ChatTransition::GlideMs));
    }
    std::printf("page-v4: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
