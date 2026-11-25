#pragma once
// Tab strip open/close motion. Header only, UI thread, no drawing, timers or HWNDs.
//
// The functional model (pages, selection, focus, hit eligibility) changes
// synchronously; this only decides where the strip is *presented*. Each tab has a
// presence in [0,1] driven by a critically damped spring. Displayed slots are laid
// out cumulatively (pitch = presence * (tab + gap)), so an opening tab pushes its
// neighbours and the new-tab button apart. A closing tab is kept as a visual-only
// "ghost" in its old place, never hit-testable: it blurs out at full width (exit:
// opacity and blur, 150 ms ease-out) under its neighbours while its slot collapses
// and they glide back together.
// Shared tab width, scroll and visible width spring between layouts only for
// structural changes (open/close, explicit reflow): window resizing, DPI changes
// and wheel scrolling snap so direct manipulation never lags.
//
// Interruptible by construction: every retarget starts from the presented value
// and velocity (Spring), so rapid opens/closes and reversals never jump.
// Reduced motion: geometry snaps; an opening tab cross-fades its surface and
// content (150 ms opacity only); a closing tab disappears immediately.
#include "motion.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace NativeChrome { namespace Motion {
// Apple-style responses (seconds, damping 1.0). Opening is discreet but readable;
// closing is a little shorter, as exits should be.
inline constexpr float TabOpenResponse = .30f, TabCloseResponse = .22f;
inline constexpr Milliseconds TabReducedFade{150};
// Exit: shorter and lighter than the entrance (opacity 1 -> 0, blur 0 -> 4 DIP).
inline constexpr Milliseconds TabExitDuration{150};
inline constexpr float TabExitBlur = 4;

// Per strip (#127 motion spec, Designer): the defaults are the strip as it shipped; the top bar and the side column set
// the spec's values (open .25 s with the content fading in over 120 ms from 40 ms; the column also closes in two beats,
// content out in 80 ms then the row folds at .22 s; the width reflow after closing on the bar at .3 s; reduced motion
// fades in 120 ms).
struct TabMotionParams {
    float openResponse = TabOpenResponse, closeResponse = TabCloseResponse, reflowResponse = TabCloseResponse;
    Milliseconds contentDelay{0}, contentFade{0};   // fade 0: the content follows the presence (TabContentOpacity)
    Milliseconds exit = TabExitDuration; bool exitThenCollapse = false;   // the slot folds once the content is gone
    Milliseconds reducedFade = TabReducedFade;
};
struct TabLayoutTarget { float tab = 0, scroll = 0, view = 0; bool overflow = false; };
struct TabItem {
    unsigned key = 0;
    int index = -1;      // live tab index, -1 for a closing ghost
    float slot = 0;      // content-space start of the tab's region (before scroll)
    float presence = 1;  // 0..1: share of the full pitch the tab occupies
    float opacity = 1;   // title / favicon / glyph opacity
    float fill = 1;      // surface emphasis factor (reduced-motion cross-fade)
    float exit = 0;      // ghosts: 0..1 through the blur-out (opacity 1-exit, blur exit*TabExitBlur)
};
struct TabLayoutFrame {
    float tab = 0, scroll = 0, view = 0, content = 0;
    std::vector<TabItem> items;
    bool moving = false;
};
// Content stays hidden while the surface is still a sliver, then eases in; the same
// curve runs backwards on close, so both directions travel the same path.
inline float TabContentOpacity(float presence) noexcept { return Ease((presence-.3f)/.7f); }

class TabStripMotion {
    struct Entry { unsigned key = 0; Spring presence{.002f}; Tween fade, exit; bool closing = false, fresh = false; Time born{}; };
    TabMotionParams params_;
    float ContentOf(const Entry& e, float presence, Time now) const noexcept {
        if (!e.fresh || params_.contentFade.count() <= 0) return TabContentOpacity(presence);
        const double t = std::chrono::duration<double, std::milli>(now - e.born).count() - params_.contentDelay.count();
        return Ease(static_cast<float>(t / params_.contentFade.count()));
    }
    bool ContentMoving(const Entry& e, Time now) const noexcept { return e.fresh && params_.contentFade.count() > 0 && now < e.born + params_.contentDelay + params_.contentFade; }
    static bool Gone(const Entry& e, Time now) noexcept { return e.closing && !e.presence.Moving(now) && !e.exit.Moving(now); }
    std::vector<Entry> entries_;
    Spring tab_{.05f}, scroll_{.05f}, view_{.05f};
    float gap_ = 0;
    bool overflow_ = false, initialized_ = false, reduced_ = false;
    bool ViewSprung(Time now) const noexcept { return overflow_ || view_.Moving(now); }
public:
    bool Initialized() const noexcept { return initialized_; }
    bool ReducedMotion() const noexcept { return reduced_; }
    std::size_t Entries() const noexcept { return entries_.size(); }
    void Reset() noexcept { entries_.clear(); initialized_ = false; }
    // Snap to the functional layout: ghosts vanish, every spring rests on target.
    // Used for startup/session restore and when reduced motion is turned on.
    void Settle() noexcept {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e){ return e.closing; }), entries_.end());
        for (auto& e : entries_) { e.presence.Snap(1); e.fade.Snap(1); e.exit.Snap(0); }
        tab_.Settle(); scroll_.Settle(); view_.Settle();
    }
    void SetReducedMotion(bool reduced) noexcept { reduced_ = reduced; if (reduced) Settle(); }
    void SetParams(const TabMotionParams& p) noexcept { params_ = p; }
    const TabMotionParams& Params() const noexcept { return params_; }

    // `keys`: stable, nonzero, never-reused tab identities in display order.
    // Returns true when this call started motion that needs frames.
    bool Sync(const std::vector<unsigned>& keys, TabLayoutTarget target, float gap, Time now, bool animateLayout = false) {
        gap_ = gap;
        if (!initialized_) {
            entries_.clear();
            for (unsigned key : keys) { Entry e; e.key = key; e.presence.Snap(1); e.fade.Snap(1); entries_.push_back(e); }
            tab_.Snap(target.tab); scroll_.Snap(target.scroll); view_.Snap(target.view);
            overflow_ = target.overflow; initialized_ = true;
            return false;
        }
        const float shownView = Sample(now).view; // presented value, before any change
        // Ghosts that finished collapsing leave the presentation for good.
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e){
            return Gone(e, now); }), entries_.end());
        const std::set<unsigned> live(keys.begin(), keys.end());
        bool opened = false, closed = false;
        std::map<unsigned, Entry> known;
        std::map<unsigned, std::vector<Entry>> after; // ghosts, attached behind the live tab that preceded them
        unsigned previous = 0;
        for (auto& e : entries_) {
            if (live.count(e.key)) {
                if (e.closing) { e.closing = false; e.fresh = false; e.presence.Set(1, now, params_.openResponse); e.exit.Set(0, now, params_.exit, false, Curve::EaseOut); opened = true; }
                previous = e.key; known.emplace(e.key, e); continue;
            }
            if (!e.closing) {
                closed = true;
                if (reduced_) continue;          // reduced motion: gone immediately
                e.closing = true; e.fresh = false; e.fade.Snap(1);
                e.presence.Set(0, params_.exitThenCollapse ? now + params_.exit : now, params_.closeResponse);   // a future start holds the slot
                e.exit.Snap(0); e.exit.Set(1, now, params_.exit, false, Curve::EaseOut);
            }
            after[previous].push_back(e);
        }
        std::vector<Entry> next;
        auto attach = [&](unsigned key){ auto it = after.find(key); if (it != after.end()) for (auto& g : it->second) next.push_back(g); };
        attach(0);
        for (unsigned key : keys) {
            auto it = known.find(key);
            if (it != known.end()) next.push_back(it->second);
            else {
                Entry e; e.key = key; opened = true;
                if (reduced_) { e.presence.Snap(1); e.fade.Snap(0); e.fade.Set(1, now, params_.reducedFade); }
                else { e.presence.Snap(0); e.presence.Set(1, now, params_.openResponse); e.fade.Snap(1); e.fresh = true; e.born = now; }
                next.push_back(e);
            }
            if (key) attach(key);
        }
        entries_ = std::move(next);

        const bool structural = opened || closed;
        const bool animate = (structural || animateLayout) && !reduced_;
        if (animate) {
            const float response = opened ? params_.openResponse : closed ? params_.closeResponse : params_.reflowResponse;
            tab_.Set(target.tab, now, response); scroll_.Set(target.scroll, now, response);
            if (ViewSprung(now) || target.overflow) {
                if (!view_.Moving(now)) view_.Snap(shownView);
                view_.Set(target.view, now, response);
            } else view_.Snap(target.view);
        } else {
            // Resize, DPI, wheel, selection scroll: follow immediately.
            if (tab_.Target() != target.tab) tab_.Snap(target.tab);
            if (scroll_.Target() != target.scroll) scroll_.Snap(target.scroll);
            if (view_.Target() != target.view) view_.Snap(target.view);
        }
        overflow_ = target.overflow;
        return structural || (animate && NextWake(now).delay.has_value());
    }

    TabLayoutFrame Sample(Time now) const {
        TabLayoutFrame f;
        f.tab = std::max(0.f, tab_.Value(now));
        float x = 0; int index = 0;
        f.items.reserve(entries_.size());
        for (const auto& e : entries_) {
            if (Gone(e, now)) continue; // collapsed and fully blurred out
            TabItem item; item.key = e.key; item.index = e.closing ? -1 : index++;
            item.presence = Clamp(e.presence.Value(now));
            item.fill = e.fade.Value(now);
            item.exit = e.exit.Value(now);
            // Ghosts keep their content at full strength: the whole tab fades as one layer.
            item.opacity = e.closing ? 1.f : ContentOf(e, item.presence, now)*item.fill;
            item.slot = x;
            x += item.presence*(f.tab+gap_);
            f.items.push_back(item);
        }
        f.content = x;
        f.view = ViewSprung(now) ? std::min(view_.Value(now), x) : x;
        f.scroll = std::clamp(scroll_.Value(now), 0.f, std::max(0.f, x-f.view));
        f.moving = NextWake(now).delay.has_value();
        return f;
    }

    Wake NextWake(Time now) const noexcept {
        if (tab_.Moving(now) || scroll_.Moving(now) || view_.Moving(now)) return Frame();
        for (const auto& e : entries_) if (e.presence.Moving(now) || e.fade.Moving(now) || e.exit.Moving(now) || ContentMoving(e, now)) return Frame();
        return {};
    }
};
}} // namespace NativeChrome::Motion
