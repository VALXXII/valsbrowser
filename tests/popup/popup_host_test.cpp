// Native fakes for the window.open request lifecycle (src/popup-target.h): no WebView2, no window.
// Build/run: python -I tests/popup/run_host_test.py
//
// A fake deferral counts its completions; a fake target records drops. Every path must complete the deferral
// exactly once, leave no pending entry, and drop the target exactly when the request did not succeed.
#include <cstdio>
#include <memory>
#include <string>

#include "../../src/popup-target.h"

namespace {
int checks = 0, failures = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Fake {
    int completed = 0, dropped = 0, released = 0, ready = 0;
    std::shared_ptr<int> com = std::make_shared<int>(7);   // stands for the retained args/environment
};
PopupHost::RequestPtr Make(PopupHost::Registry& reg, Fake& f, const void* owner, unsigned opener, unsigned target) {
    auto r = std::make_shared<PopupHost::Request>();
    r->owner = owner; r->opener = opener; r->target = target;
    r->complete = [&f] { ++f.completed; };
    r->dropTarget = [&f] { ++f.dropped; };
    r->release = [&f] { ++f.released; };
    r->com = f.com;
    reg.Add(r);
    return r;
}
const int ownerA = 0, ownerB = 0;   // distinct addresses
}  // namespace

int main() {
    // Success: barrier controller + 3 scripts, assignment, finish once; late steps and finishes are no-ops.
    {
        PopupHost::Registry reg; Fake f; auto r = Make(reg, f, &ownerA, 1, 2);
        std::weak_ptr<int> com = f.com; f.com.reset();
        r->ready = [&] { ++f.ready; r->assigned = true; reg.Finish(r, true); };
        PopupHost::Registry::Arm(r, 1);                 // controller
        PopupHost::Registry::Arm(r, 3);                 // scrollbar, vault, audio
        reg.Step(r, true); reg.Step(r, true); reg.Step(r, true);
        CHECK(f.ready == 0 && reg.Size() == 1 && reg.Pending(&ownerA));
        reg.Step(r, true);                              // last one: ready runs
        CHECK(f.ready == 1 && f.completed == 1 && f.dropped == 0 && f.released == 1 && reg.Size() == 0 && !reg.Pending(&ownerA));
        CHECK(com.expired());                           // retained COM state released by Finish
        CHECK(!reg.Finish(r, false) && !reg.Finish(r, true));
        reg.Step(r, true); reg.Step(r, false);          // late completions after the end
        CHECK(f.completed == 1 && f.dropped == 0 && f.ready == 1);
    }
    // A failed script / null controller / failed assignment: failure once, target dropped once.
    for (int failAt = 0; failAt < 4; ++failAt) {
        PopupHost::Registry reg; Fake f; auto r = Make(reg, f, &ownerA, 1, 2);
        r->ready = [&] { ++f.ready; reg.Finish(r, true); };
        PopupHost::Registry::Arm(r, 4);
        for (int i = 0; i < 4; ++i) reg.Step(r, i != failAt);
        CHECK(f.ready == 0 && f.completed == 1 && f.dropped == 1 && f.released == 1 && reg.Size() == 0);
    }
    // Ready itself failing (assignment / post-assignment hook): finishes as failure, once.
    {
        PopupHost::Registry reg; Fake f; auto r = Make(reg, f, &ownerA, 1, 2);
        r->ready = [&] { reg.Finish(r, false); reg.Finish(r, true); };
        PopupHost::Registry::Arm(r, 1); reg.Step(r, true);
        CHECK(f.completed == 1 && f.dropped == 1 && reg.Size() == 0);
    }
    // Cancellation by tab identity: the opener closing, or the pending target closing; others untouched.
    {
        PopupHost::Registry reg; Fake f1, f2, f3;
        auto a = Make(reg, f1, &ownerA, 10, 11), b = Make(reg, f2, &ownerA, 20, 21), c = Make(reg, f3, &ownerB, 10, 31);
        CHECK(reg.CancelTab(10) == 2);                  // opener 10 owns a and c
        CHECK(f1.completed == 1 && f3.completed == 1 && f2.completed == 0 && reg.Size() == 1);
        CHECK(reg.CancelTab(21) == 1 && f2.completed == 1 && f2.dropped == 1 && reg.Size() == 0);
        CHECK(reg.CancelTab(10) == 0 && reg.CancelTab(0) == 0);
        (void)a; (void)b; (void)c;
    }
    // Cancellation by owner (cold / disposal / engine failure) and of everything (window close, halt).
    {
        PopupHost::Registry reg; Fake f1, f2, f3;
        Make(reg, f1, &ownerA, 1, 2); Make(reg, f2, &ownerB, 3, 4); Make(reg, f3, &ownerA, 5, 6);
        CHECK(reg.Pending(&ownerA) && reg.Pending(&ownerB));
        CHECK(reg.CancelOwner(&ownerA) == 2 && !reg.Pending(&ownerA) && reg.Pending(&ownerB));
        CHECK(f1.completed == 1 && f3.completed == 1 && f2.completed == 0);
        CHECK(reg.CancelAll() == 1 && f2.completed == 1 && reg.Size() == 0);
        CHECK(reg.CancelAll() == 0);
    }
    // Reentrancy: completing the deferral runs nested code that cancels again: still once.
    {
        PopupHost::Registry reg; Fake f; auto r = Make(reg, f, &ownerA, 1, 2);
        r->complete = [&] { ++f.completed; reg.CancelAll(); reg.Finish(r, false); reg.CancelTab(2); };
        r->dropTarget = [&] { ++f.dropped; reg.CancelOwner(&ownerA); };
        CHECK(reg.CancelTab(1) == 1);
        CHECK(f.completed == 1 && f.dropped == 1 && reg.Size() == 0 && reg.Finished() == 1);
    }
    // A request cancelled while its controller is still being created: the late barrier steps do nothing.
    {
        PopupHost::Registry reg; Fake f; auto r = Make(reg, f, &ownerA, 1, 2);
        r->ready = [&] { ++f.ready; };
        PopupHost::Registry::Arm(r, 2);
        reg.CancelOwner(&ownerA);
        PopupHost::Registry::Arm(r, 1);                 // arming after the end is ignored
        reg.Step(r, true); reg.Step(r, true); reg.Step(r, true);
        CHECK(f.ready == 0 && f.completed == 1 && f.dropped == 1);
    }
    // Admission: unchanged HTTP(S), the exact blank document; everything else refused.
    {
        auto sandbox = [](const std::wstring& u) { return u.rfind(L"https://browser-debug.invalid", 0) == 0; };
        bool (*isSandbox)(const std::wstring&) = sandbox;
        CHECK(PopupHost::AdmitInitialUri(L"https://example.com/pay", isSandbox));
        CHECK(PopupHost::AdmitInitialUri(L"http://127.0.0.1:8080/x", isSandbox));
        CHECK(PopupHost::AdmitInitialUri(L"about:blank", isSandbox));
        for (const wchar_t* bad : {L"", L"about:blank#x", L"about:srcdoc", L"file:///C:/x", L"data:text/html,x", L"javascript:1",
                                   L"https://browser-debug.invalid/", L"edge://settings", L"ms-settings:", L"HTTPS://example.com"})
            CHECK(!PopupHost::AdmitInitialUri(bad, isSandbox));
    }
    std::printf("popup_host_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
