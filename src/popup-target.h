#pragma once
// Script-opened windows (window.open, target=_blank): the request's lifecycle, independent of COM so native
// fakes can test it (tests/popup/popup_host_test.cpp). design/popup-window-target-report.md.
//
// One Request per NewWindowRequested the host accepts. The engine's deferral is held from the event until the
// target is assigned (put_NewWindow) and its post-assignment hooks are installed, or until the request fails.
// Finish() is the ONLY way out, and it runs exactly once whatever ends the request (success, any failure, the
// opener or target tab closing, the owning profile going cold/disposed, window close, terminal halt):
//   terminal flag first (reentrancy: a completion that runs nested code cannot finish twice), out of the
//   registry, the deferral completed, then the request arguments/environment released; on failure the target
//   placeholder is dropped (never a recently-closed entry, never the window's last-tab close).
// A late controller or script completion arriving after Finish sees `terminal` and only cleans up.
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace PopupHost {

struct Request {
    const void* owner = nullptr;       // the opener's workspace (profile) — the target is created there, always
    unsigned opener = 0, target = 0;   // tab serials (identity; indexes move)
    bool terminal = false, assigned = false, userInitiated = false, blank = false, qaHeld = false;
    int waits = 0;                     // readiness barrier: controller setup + each initial document script
    std::function<void()> complete;    // completes and releases the engine deferral (called exactly once)
    std::function<void()> release;     // releases the retained args / captured environment
    std::function<void()> dropTarget;  // failure: removes the placeholder / closes the unassigned or failed target
    std::function<void()> ready;       // barrier reached zero: revalidate, assign, post-assignment hooks, Finish
    std::shared_ptr<void> com;         // the host's COM state (args, deferral guard, environment): released by Finish
};
using RequestPtr = std::shared_ptr<Request>;

class Registry {
public:
    void Add(const RequestPtr& r) { items_.push_back(r); }
    std::size_t Size() const { return items_.size(); }
    bool Pending(const void* owner) const {
        for (const auto& r : items_) if (r->owner == owner) return true;
        return false;
    }
    // Exactly once. success=false drops the target. Returns false when the request had already finished.
    bool Finish(const RequestPtr& r, bool success) {
        if (!r || r->terminal) return false;
        r->terminal = true;
        for (auto it = items_.begin(); it != items_.end(); ++it) if (*it == r) { items_.erase(it); break; }
        ++finished_;
        if (auto c = std::move(r->complete)) c();
        r->complete = nullptr;
        if (!success) { if (auto d = std::move(r->dropTarget)) d(); }
        r->dropTarget = nullptr; r->ready = nullptr;
        if (auto rel = std::move(r->release)) rel();
        r->release = nullptr;
        r->com.reset();   // last: its deferral guard completes in its destructor if `complete` was never set
        return true;
    }
    // Cancels (failure) every pending request matching `pred`; a snapshot, so Finish may run nested code safely.
    template <class P> int CancelIf(P pred) {
        std::vector<RequestPtr> victims;
        for (const auto& r : items_) if (pred(*r)) victims.push_back(r);
        int n = 0;
        for (auto& r : victims) n += Finish(r, false) ? 1 : 0;
        return n;
    }
    int CancelOwner(const void* owner) { return CancelIf([owner](const Request& r) { return r.owner == owner; }); }
    int CancelTab(unsigned serial) { return CancelIf([serial](const Request& r) { return serial && (r.opener == serial || r.target == serial); }); }
    int CancelAll() { return CancelIf([](const Request&) { return true; }); }
    unsigned Finished() const { return finished_; }

    // Readiness barrier. Arm(n) before starting n asynchronous steps; Step(ok) when each ends.
    static void Arm(const RequestPtr& r, int n) { if (r && !r->terminal) r->waits += n; }
    void Step(const RequestPtr& r, bool ok) {
        if (!r || r->terminal) return;
        if (!ok) { Finish(r, false); return; }
        if (--r->waits == 0 && r->ready) { auto go = r->ready; go(); }
    }

private:
    std::vector<RequestPtr> items_;
    unsigned finished_ = 0;
};

// Initial target the host admits (unchanged HTTP(S) admission, plus the exact blank document). Everything else is
// refused while still marked handled: file:, data:, javascript:, internal/debug pages, other schemes.
inline bool AdmitInitialUri(const std::wstring& uri, bool (*isSandbox)(const std::wstring&)) {
    if (uri == L"about:blank") return true;
    const bool web = uri.rfind(L"https://", 0) == 0 || uri.rfind(L"http://", 0) == 0;
    return web && !(isSandbox && isSandbox(uri));
}

}  // namespace PopupHost
