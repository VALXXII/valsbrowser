#pragma once
// Chat intégré — Mode Commentaire: where the bubble goes (pure, no Win32 window; tests/chat/chat_units_test.cpp).
// design/chat-integration.md « Mode Commentaire ». Paper 18NS-0 / 1AWI-0: the bubble sits in the free space next to the
// element it talks about — at its right if it fits, else at its left, else under it, else above it — and never leaves
// the page area. All rectangles in the same unit (screen pixels of the host).
#include <algorithm>
#include <cmath>
#include <string>

namespace ChatBubble {
struct Rect { int left = 0, top = 0, right = 0, bottom = 0; int W() const { return right - left; } int H() const { return bottom - top; } };
enum class Side { Right, Left, Below, Above, Inside };
// fromRight / fromBottom: the edge that stays when the bubble changes size (Left: its right edge; Above: its bottom;
// Inside: the corner it was put in, R44). why: one line for the log (R44: the side chosen and why).
struct Placement { Rect rect; Side side = Side::Right; bool fromRight = false, fromBottom = false; const char* why = ""; int slid = 0;
                   int limit = 0; };   // limit (#130 C rule 10): the height its side can hold, when that is less than planned (its thread scrolls); 0: none   // slid: px along its side (decision 1)
struct Point { int x = 0, y = 0; bool set = false; };

inline int Clamp(int v, int lo, int hi) { return hi < lo ? lo : std::max(lo, std::min(v, hi)); }
// The bubble's size, bounded by the page area (minus the margins) and by the panel page's own maxima.
inline void Fit(int& w, int& h, const Rect& area, int margin, int maxW, int maxH) {
    w = std::max(1, std::min({w, maxW, area.W() - 2 * margin}));
    h = std::max(1, std::min({h, maxH, area.H() - 2 * margin}));
}
// element: the element's box; size: the bubble's (w, h); area: the page's visible area; gap: between the element and the
// bubble (12); `lead` (4): Paper puts the bubble's top 4 px above the element's top at its side (on the selection frame),
// its left 4 px before the element's left below / above it.
// Placement = Paper, always (coordinator, the user saw « bulle trop loin »): at the right / left, the edge 12 px from the
// element and the bubble's top = the element's top − 4, exactly; below / above, 12 px from its edge and its left at − 4;
// inside, 12 px (`corner`) from both edges of the chosen corner, in the element. Never moved to keep room for later.
// A side is taken only if the bubble, at its CURRENT height, is whole in the visible area from that exact position: right,
// left, below, above; else inside the element, its visible corner nearest to the click. Then (R29) it grows from its fixed
// edge; past the visible area the user scrolls. `prefer`: the side of the open bubble, kept while it still fits.
// #130 C: a side with no room under the bubble — slid up along that side (`margin` from the page area's edges), before
// below / above (no flip above the element). No free side at all (rules 9, 10): below, else above, never over the element
// (its height limited to its side's room), and inside it (bottom right corner) only when it fills the page area.
// `minH`: the least height worth a side (the composer's); `click` and `corner` are no longer used.
inline Placement Place(const Rect& element, int w, int h, const Rect& area, int gap, int margin, Side prefer = Side::Right, int lead = 0,
                       int hPlan = 0, Point click = {}, int corner = 0, int minH = 0) {
    (void)click; (void)corner;   // rule 9: inside the element the corner is the bottom right one, `margin` from its edges (no longer the click's)
    // the height planned for: the one the bubble WILL have (hPlan: its conversation's last height, or 520), at least its current one
    const int H = std::min(std::max(h, hPlan), area.H());
    auto exact = [&](Side s) -> Placement {   // Paper's exact positions; the rect has the current height, on the edge that stays
        Placement p; p.side = s;
        switch (s) {
        case Side::Right: p.rect = {element.right + gap, element.top - lead, element.right + gap + w, element.top - lead + h}; p.why = "right of the element (Paper: +12, top -4): the whole bubble fits there"; break;
        case Side::Left: p.rect = {element.left - gap - w, element.top - lead, element.left - gap, element.top - lead + h}; p.fromRight = true; p.why = "left of the element (Paper: -12, top -4): no room at its right"; break;
        case Side::Below: p.rect = {element.left - lead, element.bottom + gap, element.left - lead + w, element.bottom + gap + h}; p.why = "below the element (Paper: +12, left -4): no room at its sides"; break;
        default: p.rect = {element.left - lead, element.top - gap - h, element.left - lead + w, element.top - gap}; p.fromBottom = true; p.side = Side::Above; p.why = "above the element (Paper: -12, left -4, growing up): no room at its sides nor below"; break;
        }
        return p;
    };
    auto planned = [&](const Placement& p) { return p.fromBottom ? Rect{p.rect.left, p.rect.bottom - H, p.rect.right, p.rect.bottom} : Rect{p.rect.left, p.rect.top, p.rect.right, p.rect.top + H}; };
    auto whole = [&](const Rect& r) { return r.left >= area.left && r.top >= area.top && r.right <= area.right && r.bottom <= area.bottom; };
    if (prefer != Side::Inside) { const auto p = exact(prefer); if (whole(planned(p))) return p; }
    for (Side s : {Side::Right, Side::Left}) { const auto p = exact(s); if (whole(planned(p))) return p; }
    // #130, variant C (Paper « #130 Bulle sans place dessous · C remonte le long du côté (validée) », Val's choice): no room
    // under it at its side — the bubble stays NEXT TO its element (same side, same 12 px gap: 8 px from the selection frame)
    // and slides up along it until it is whole, its bottom `margin` (8 px) above the page area's bottom, never higher than
    // `margin` under the banner (the page area's top); never above the element (no flip). Right first, then left.
    for (Side s : {Side::Right, Side::Left}) {
        auto p = exact(s);
        if (p.rect.left < area.left || p.rect.right > area.right) continue;
        const int Hc = std::max(1, std::min(H, area.H() - 2 * margin));   // rule 4: at most the page area - 2 x margin (its thread scrolls)
        const int top0 = p.rect.top, top1 = Clamp(top0, area.top + margin, area.bottom - margin - Hc);
        p.rect.top = top1; p.rect.bottom = top1 + std::min(h, Hc); p.slid = std::abs(top1 - top0); if (Hc < H) p.limit = Hc;
        p.why = s == Side::Right ? "right of the element, slid along it to be whole (#130 C: +12, 8 px from the page area's edges)"
                                 : "left of the element, slid along it to be whole (#130 C: -12, 8 px from the page area's edges)";
        return p;
    }
    // #130 C, rules 9 and 10 (Paper « #130 Bulle sans place dessous · C règles (validées) »): an element with NO FREE SIDE (as
    // wide as the page: section.hero, a title) — the bubble goes below it, else above it, 12 px from it (its left at Paper's
    // -4, slid to stay `margin` from the page area's edges), `margin` (8 px) from the page area's bottom / from the banner.
    // It never goes over the element: when neither side holds its height, it takes the side with the most room and its
    // height is limited to that room (its thread scrolls, rule 4). Only when no side has even `minH` — the element fills the
    // page area — it sits inside the element, at the bottom right corner of what is visible of it, `margin` from its edges.
    {   const int roomBelow = (area.bottom - margin) - (element.bottom + gap), roomAbove = (element.top - gap) - (area.top + margin);
        const int least = std::max(1, minH > 0 ? std::min(minH, H) : std::min(h, H));
        const int x = Clamp(element.left - lead, area.left + margin, area.right - margin - w);
        auto side = [&](Side s, int room) {
            Placement p; p.side = s; p.slid = std::abs(x - (element.left - lead));
            const int hh = std::min(h, room);
            if (s == Side::Below) { p.rect = {x, element.bottom + gap, x + w, element.bottom + gap + hh}; }
            else { p.rect = {x, element.top - gap - hh, x + w, element.top - gap}; p.fromBottom = true; }
            if (room < H) p.limit = room;
            p.why = s == Side::Below ? (room < H ? "below the element (#130 C rule 10: +12, never over it), its height limited to the room there"
                                                 : "below the element (#130 C rule 9: +12, left -4): no free side")
                                     : (room < H ? "above the element (#130 C rule 10: -12, never over it), its height limited to the room there"
                                                 : "above the element (#130 C rule 9: -12, left -4, growing up): no free side, no room below");
            return p;
        };
        if (w <= area.W()) {
            if (prefer == Side::Above && roomAbove >= H) return side(Side::Above, roomAbove);   // the side it flipped to (rule 10) is kept while it holds
            if (roomBelow >= H) return side(Side::Below, roomBelow);
            if (roomAbove >= H) return side(Side::Above, roomAbove);
            if (std::max(roomBelow, roomAbove) >= least) return roomBelow >= roomAbove ? side(Side::Below, roomBelow) : side(Side::Above, roomAbove);
        }
    }
    // inside the element (rule 9, last): the bottom right corner of its visible part, `margin` from both edges, growing up;
    // whole in the page area (`margin` from its edges), at most the page area − 2 × margin high
    const Rect vis{std::max(element.left, area.left), std::max(element.top, area.top), std::min(element.right, area.right), std::min(element.bottom, area.bottom)};
    const int room = area.H() - 2 * margin, hh = std::max(1, std::min(h, room));
    int right = vis.right - margin, bottom = vis.bottom - margin;
    const int r0 = right, b0 = bottom;
    right = Clamp(right, area.left + margin + w, area.right - margin); bottom = Clamp(bottom, area.top + margin + hh, area.bottom - margin);
    Placement p; p.side = Side::Inside; p.fromRight = true; p.fromBottom = true; p.slid = std::abs(right - r0) + std::abs(bottom - b0);
    p.rect = {right - w, bottom - hh, right, bottom};
    if (bottom - (area.top + margin) < H) p.limit = bottom - (area.top + margin);
    p.why = "inside the element, bottom right corner (#130 C rule 9: 8 px from its edges): the element fills the page area, no side has room";
    return p;
}
// #130 C rule 10 (never over the element: the bubble switches side, above <-> below): a bubble below (above) its element that
// is as high as its side allows — `h` reached `cap`, what was offered — goes to the other side when that one holds more.
// Returns the side to be on (the same one: nothing to do). No way back and forth: it only ever goes to the roomier side.
inline Side FlipSide(Side side, int h, int cap, int roomBelow, int roomAbove) {
    if (side != Side::Below && side != Side::Above) return side;
    if (cap <= 0 || h < cap - 1) return side;   // not limited by its side
    const int mine = side == Side::Below ? roomBelow : roomAbove, other = side == Side::Below ? roomAbove : roomBelow;
    if (mine - cap > 2) return side;   // what was offered is the bubble's own maximum (520, the page area - 16), not its side's room
    return other > mine + 1 && other > h + 1 ? (side == Side::Below ? Side::Above : Side::Below) : side;
}
// The room of an element's sides, as Place() counts it (rule 9): from 12 px (`gap`) off the element to `margin` off the page area.
inline int RoomBelow(const Rect& element, const Rect& area, int gap, int margin) { return (area.bottom - margin) - (element.bottom + gap); }
inline int RoomAbove(const Rect& element, const Rect& area, int gap, int margin) { return (element.top - gap) - (area.top + margin); }
// R2: an element carries its frame + icon (Paper 18IJ-0) only once a message left its bubble — sent, waiting in the
// queue, or kept as an annotation (Ctrl+Entrée). A click on an element then closing its bubble leaves nothing on it.
inline bool SentItem(const std::string& type) { return type == "message.user"; }
template <class Types> bool Sent(const Types& itemTypes, bool queued) { if (queued) return true; for (const auto& t : itemTypes) if (SentItem(t)) return true; return false; }
// R6: a click on an element that has a conversation. Its conversation opens in the bubble when it is in memory or can be
// read from the history; when it cannot (never saved, removed), the element is NOT left without a bubble: it forgets that
// conversation and opens a new one (before: « conversation not found » and nothing happened).
enum class Reopen { Shown, FromHistory, Fresh };
inline Reopen ReopenOf(bool inMemory, bool readable) { return inMemory ? Reopen::Shown : readable ? Reopen::FromHistory : Reopen::Fresh; }
// R3 (contract B §20): the page asks for room around its surface while a menu or a tooltip goes out of it
// (`bubble.size` overflowTop / Right / Bottom / Left, CSS px); the transparent window grows by that much around the
// surface, which stays in place, as far as the screen allows. `inset` (CSS px) is what was granted: the page draws its
// surface at (inset.left, inset.top) and turns a menu that does not fit.
struct Edges {
    int top = 0, right = 0, bottom = 0, left = 0;
    bool operator==(const Edges& o) const { return top == o.top && right == o.right && bottom == o.bottom && left == o.left; }
};
inline constexpr int MaxOverflowCss = 400;
inline constexpr int PageInsetCapCss = 1000;   // = panel.js applyInset CAP (P, 1000): the page draws the surface up to this far into its window — room to grow 520 + margin 400 fits, never framed again
inline Edges Bounded(Edges e) { auto b = [](int v) { return Clamp(v, 0, MaxOverflowCss); }; return {b(e.top), b(e.right), b(e.bottom), b(e.left)}; }
struct Grown { Rect window; Edges inset; };
// surface: the surface on screen (px); want: CSS px; screen: the work area of the monitor (px); k: px per CSS px.
inline Grown Grow(const Rect& surface, const Edges& want, const Rect& screen, float k) {
    auto px = [&](int css) { return int(css * k + .5f); };
    auto css = [&](int p) { return k > 0 ? int(p / k) : 0; };   // granted rounds down: never more than the window has
    const int t = std::max(0, std::min(px(want.top), surface.top - screen.top)), l = std::max(0, std::min(px(want.left), surface.left - screen.left));
    const int b = std::max(0, std::min(px(want.bottom), screen.bottom - surface.bottom)), r = std::max(0, std::min(px(want.right), screen.right - surface.right));
    Grown g; g.window = {surface.left - l, surface.top - t, surface.right + r, surface.bottom + b};
    g.inset = {css(t), css(r), css(b), css(l)};
    // the surface must start exactly at the inset the page is told: the window's top-left follows the granted CSS values
    g.window.left = surface.left - px(g.inset.left); g.window.top = surface.top - px(g.inset.top);
    return g;
}
// R19 (windowed WebView: a window that grows left / up cannot move its page in the same frame): the bubble's window is
// the surface plus a FIXED transparent margin (up to `marginCss` per side, as far as the screen allows) and as tall as
// the surface may grow (maxH), on the side that stays fixed (`bottomFixed`: side « above »). A menu or a tooltip that
// leaves the surface does not move or resize anything: only the window's region (what is shown and clickable) widens
// to the surface + the overflow asked, within the margin. At rest the region is the surface: clicks around it reach
// the page. `inset` (CSS px) says where the surface is in the window.
struct Framed { Rect window; Edges inset; Rect region; };   // region: in the window's own pixels
// R39: the same window (size, inset) carried by the surface: a scroll step only moves it — no new inset for the page,
// no new size for the WebView. It may then leave the monitor's work area: that part is transparent and outside the
// region (clipped to the page area by the caller).
inline Framed Reframe(const Rect& surface, const Edges& inset, int winW, int winH, float k, const Edges& over) {
    auto px = [&](int css) { return int(css * k + .5f); };
    Framed f; f.inset = inset;
    const int sx = px(inset.left), sy = px(inset.top);
    f.window = {surface.left - sx, surface.top - sy, surface.left - sx + winW, surface.top - sy + winH};
    f.region = {sx - px(std::min(over.left, inset.left)), sy - px(std::min(over.top, inset.top)),
                sx + surface.W() + px(std::min(over.right, inset.right)), sy + surface.H() + px(std::min(over.bottom, inset.bottom))};
    f.region.left = std::max(0, f.region.left); f.region.top = std::max(0, f.region.top);
    f.region.right = std::min(winW, f.region.right); f.region.bottom = std::min(winH, f.region.bottom);
    return f;
}
// E4 (review R): during an opening the window keeps its place relative to the surface's FIXED corner and its size — the
// frame chosen at the opening, carried by every scroll and every new size, so its inset never changes because the screen's
// edge got nearer (it changed at the first new size after a scroll: the surface jumped by the scroll for a few images).
// `rel`: the window relative to the fixed corner (left/top of `rel` are offsets, right/bottom too). ok = false when the
// surface (with what its overflow asks) no longer fits in that window: framed again then.
struct Kept { bool ok = false; Framed f; };
inline Rect CornerOf(const Rect& surface, bool fromRight, bool fromBottom) { const int x = fromRight ? surface.right : surface.left, y = fromBottom ? surface.bottom : surface.top; return {x, y, x, y}; }
inline Rect RelativeTo(const Rect& window, const Rect& corner) { return {window.left - corner.left, window.top - corner.top, window.right - corner.left, window.bottom - corner.top}; }
inline Kept FrameKept(const Rect& surface, bool fromRight, bool fromBottom, const Rect& rel, float k, const Edges& over) {
    auto css = [&](int p) { return k > 0 ? int(p / k) : 0; };
    auto px = [&](int c) { return int(c * k + .5f); };
    const Rect c = CornerOf(surface, fromRight, fromBottom);
    Rect w{c.left + rel.left, c.top + rel.top, c.left + rel.right, c.top + rel.bottom};
    Kept out;
    if (surface.left < w.left || surface.top < w.top || surface.right > w.right || surface.bottom > w.bottom) return out;
    Edges inset{css(surface.top - w.top), css(w.right - surface.right), css(w.bottom - surface.bottom), css(surface.left - w.left)};
    if (inset.top > PageInsetCapCss || inset.left > PageInsetCapCss) return out;   // Q r06: the page would draw it elsewhere — framed again
    const int dx = (surface.left - px(inset.left)) - w.left, dy = (surface.top - px(inset.top)) - w.top;   // the surface exactly at the inset told
    w.left += dx; w.right += dx; w.top += dy; w.bottom += dy;
    inset.right = css(w.right - surface.right); inset.bottom = css(w.bottom - surface.bottom);
    if (px(over.top) > surface.top - w.top || px(over.left) > surface.left - w.left || px(over.right) > w.right - surface.right || px(over.bottom) > w.bottom - surface.bottom) {
        const Edges b = Bounded(over);
        // Q pass 1, defect 5 (E4): a menu the window cannot hold any more no longer frames it again (the surface moved for a few
        // images): the window gives what it has, the page turns its menu (R3, inset)
        (void)b;
    }
    out.ok = true; out.f = Reframe(surface, inset, w.W(), w.H(), k, over); return out;
}
inline Framed FrameBubble(const Rect& surface, int maxH, bool bottomFixed, const Rect& screen, float k, const Edges& over, int marginCss = 400) {
    Rect full = surface;
    if (bottomFixed) full.top = std::min(surface.top, surface.bottom - maxH); else full.bottom = std::max(surface.bottom, surface.top + maxH);
    const Grown g = Grow(full, Edges{marginCss, marginCss, marginCss, marginCss}, screen, k);
    auto px = [&](int css) { return int(css * k + .5f); };
    auto css = [&](int p) { return k > 0 ? int(p / k) : 0; };
    Framed f; f.window = g.window;
    // Q's real-window failure (r44 / r06 / r47): the page never draws its surface further than PageInsetCapCss into the
    // window (panel.js CAP). A bottom-fixed bubble asked room to grow up (up to 520) + the margin: an inset of ~560, drawn at
    // 400 — the surface 160 CSS px above the region shown, nothing on screen. No side of the window goes further than that.
    // Only the top and left insets place the drawing (right / bottom only bound the page's menus): those two are capped.
    const int cap = px(PageInsetCapCss);
    f.window.left = std::max(f.window.left, surface.left - cap); f.window.top = std::max(f.window.top, surface.top - cap);
    f.inset.left = css(surface.left - f.window.left); f.inset.top = css(surface.top - f.window.top);
    f.window.left = surface.left - px(f.inset.left); f.window.top = surface.top - px(f.inset.top);   // the surface exactly at the inset told
    f.inset.right = css(f.window.right - surface.right); f.inset.bottom = css(f.window.bottom - surface.bottom);
    return Reframe(surface, f.inset, f.window.W(), f.window.H(), k, over);
}
// D1 (Paper 18NS-0 … 19IO-0, bubble open): the element's frame `18QJ-0` goes 4 px out of the element on every side;
// its two labels (`18QK-0` selector, `18QQ-0` size) sit 24 px above the element, 20 px high, flush with the frame's
// left and right edges. The size label says the element's real size, « 520x44 » (a small x, as Paper writes it).
inline std::string SizeLabel(double w, double h) { auto r = [](double v) { return std::to_string(long(v < 0 ? 0 : v + .5)); }; return r(w) + "x" + r(h); }
inline constexpr int FrameOutset = 4, LabelRise = 24, LabelHeight = 20;
// D3 (brief rule 6): in Mode Commentaire the launcher (bottom right) only serves the conversations whose element is
// gone from the page: shown only when there is one, counting only those. `onPage`: the ids whose element is here.
template <class Ids, class Set> int GoneCount(const Ids& anchoredIds, const Set& onPage) { int n = 0; for (const auto& id : anchoredIds) if (!onPage.count(id)) ++n; return n; }
// D1 after a reload (the agent's edit reloads the page): the page script starts empty. What the host gives it again:
// the conversations' marks (Mode Commentaire), and, while the bubble is open, its element to follow and draw (frame +
// labels, no hover frame).
struct ScriptRestore { bool marks = false, follow = false; };
inline ScriptRestore OnScriptIn(bool commentLayout, bool bubbleOpen) { return {commentLayout, commentLayout && bubbleOpen}; }
// R1 / accessibility: how the chat page is hosted. Windowed by default — screen readers read the page natively; the
// visual hosting (DirectComposition) gave UI Automation an empty page and cut the main window's tree (19 Nov 2026).
// `visualSwitch`: the value of BROWSER_CHAT_VISUAL ("" when unset); "1" brings the visual hosting back.
inline bool VisualHosting(const std::wstring& visualSwitch) { return visualSwitch == L"1"; }
// The colour behind the page is transparent when the page is a visual, or sits in the bubble's see-through window
// (the page draws its own 16 px surface); else it is the canvas #171717 (the panel: no white flash while it loads).
inline bool TransparentBackground(bool visual, bool inBubble) { return visual || inBubble; }
// R29 / R38: once placed, the bubble keeps its anchor for the whole conversation — the same offset from its element,
// the same fixed edge (its top; its bottom when it is above the element). A new card, an open diff, an expanded
// permission or error only make it grow away from that edge. #130 wins over R29 (the bubble is always whole in the window):
// a new size that takes it past the bottom of the page area slides it up along its element (`PastBottom`, `SlideUp`); and
// at the end of the document it moves up by what is missing (`BottomShift`).
struct Anchor { bool set = false; Side side = Side::Right; bool fromRight = false, fromBottom = false; int dx = 0, dy = 0; };   // dy: from the element's top to the fixed edge
inline Anchor AnchorOf(const Placement& p, const Rect& element) {
    Anchor a; a.set = true; a.side = p.side; a.fromRight = p.fromRight || p.side == Side::Left; a.fromBottom = p.fromBottom || p.side == Side::Above;
    a.dx = (a.fromRight ? p.rect.right : p.rect.left) - element.left;   // R40: at its left, the right edge stays
    a.dy = (a.fromBottom ? p.rect.bottom : p.rect.top) - element.top; return a;
}
inline Rect Anchored(const Anchor& a, const Rect& element, int w, int h) {
    Rect r;
    if (a.fromRight) { r.right = element.left + a.dx; r.left = r.right - w; } else { r.left = element.left + a.dx; r.right = r.left + w; }
    if (a.fromBottom) { r.bottom = element.top + a.dy; r.top = r.bottom - h; } else { r.top = element.top + a.dy; r.bottom = r.top + h; }
    return r;
}
// #130 (the user: « la bulle reste toujours entièrement dans la fenêtre »; it wins over R29): a new size that takes the
// bubble closer than `margin` (8 px) to the bottom of the page area while its element is on screen (not a scroll: the
// user's own; rule 5 of the board, recomputing during a scroll, comes later) — variant C (Paper « #130 Bulle sans place
// dessous · C remonte le long du côté (validée) »): at its side (or over its element) it slides up along it by what is
// missing (`SlideUp`), never higher than `margin` under the banner; at most page area − 2 × margin high, its thread scrolls
// inside with the top fade (Paper 1ANU-0), its input and foot always shown.
inline bool PastBottom(const Rect& surface, const Rect& element, const Rect& area, int margin = 0) {
    return surface.H() > 0 && surface.bottom > area.bottom - margin && element.bottom > area.top && element.top < area.bottom;
}
inline int SlideUp(const Rect& surface, const Rect& area, int margin) {
    const int over = surface.bottom - (area.bottom - margin);
    return over > 0 ? std::min(over, std::max(0, surface.top - (area.top + margin))) : 0;
}
inline int BottomShift(const Rect& surface, const Rect& area, int margin, bool atDocumentEnd) {
    const int over = surface.bottom - (area.bottom - margin);
    return atDocumentEnd && over > 0 ? std::min(over, std::max(0, surface.top - (area.top + margin))) : 0;
}
// Q pass 1, defect 6 (E1): an element without a safe selector — in a same-origin iframe, or in a web component's shadow
// root — is found again by this key (frame kind, shadow path, selector inside). Nothing to key: "" (cross-origin, no selector).
inline std::string UnsafeAnchorKey(const std::string& frameKind, const std::string& selector, const std::vector<std::string>& shadowPath) {
    if (selector.empty() || frameKind == "cross-origin") return "";
    std::string k = frameKind + "|"; for (const auto& s : shadowPath) k += s + ">>"; return k + "|" + selector;
}
// A bubble asked open while one is open: the same one (kept as it is) only for the same conversation — or, when a
// conversation is not known yet on one side, the same element. Anything else is another element: a new opening.
inline bool SameBubble(const std::string& openConv, const std::string& openSelector, const std::string& conv, const std::string& selector) {
    if (!openConv.empty() && !conv.empty()) return openConv == conv;
    return !selector.empty() && selector == openSelector;
}
// R40: the largest size the bubble may be resized to from its fixed corner, staying in the page area (CSS px).
struct ResizeMax { int width = 0, height = 0; };
inline ResizeMax ResizeRoom(const Rect& surface, bool fromRight, bool fromBottom, const Rect& area, int margin, float k) {
    auto css = [&](int px) { return k > 0 ? std::max(0, int(px / k)) : 0; };
    ResizeMax m;
    m.width = css(fromRight ? surface.right - (area.left + margin) : (area.right - margin) - surface.left);
    m.height = css(fromBottom ? surface.bottom - (area.top + margin) : (area.bottom - margin) - surface.top);
    return m;
}
inline ResizeMax ResizeRoom(const Rect& surface, Side side, const Rect& area, int margin, float k) { return ResizeRoom(surface, side == Side::Left, side == Side::Above, area, margin, k); }
// R44: the whole surface is in the visible area (a click on its element checks it, and places it again if not).
inline bool FullyVisible(const Rect& surface, const Rect& area) {
    return surface.W() > 0 && surface.H() > 0 && surface.left >= area.left && surface.top >= area.top && surface.right <= area.right && surface.bottom <= area.bottom;
}
// R38: the height the bubble may take is the room between its fixed edge and the page area's far side (its content
// scrolls inside beyond that): a diff, an expanded permission or error opens in place and never makes it move.
inline int RoomCss(const Rect& surface, bool bottomFixed, const Rect& area, int margin, float k, int maxCss, int minCss = 120) {
    const int px = bottomFixed ? surface.bottom - (area.top + margin) : (area.bottom - margin) - surface.top;
    const int css = k > 0 ? int(px / k) : 0;
    return std::max(minCss, std::min(maxCss, css));
}
// R47 (test réel: an authorization cut and greyed after scrolling up then down): the height offered to the page is the
// room measured when the bubble is placed (R44: where its 520 px fit), then kept with the anchor — a scroll never makes
// it smaller (before: measured again at each new size from where the scroll had put the bubble, down to 120 px, and
// never given back). `placedNow`: this placement chose the anchor; `kept`: the room given then (0 = none yet).
inline int OfferedRoomCss(bool placedNow, int kept, const Rect& surface, bool bottomFixed, const Rect& area, int margin, float k, int maxCss) {
    if (placedNow || kept <= 0) return RoomCss(surface, bottomFixed, area, margin, k, maxCss);
    return kept;
}
// #130 C rule 7 (Paper « C règles (validées) »): the browser never scrolls the user's page to show a card — the old R47 fix
// (RevealScrollCss) is removed. A card's buttons are in the bubble's foot, always shown (rule 8), and the bubble is whole in
// the page area (rules 2 to 4, 9, 10).
// R29: what of the window may show — never over the browser's own bars (the page area, in window pixels).
inline Rect Clip(const Rect& a, const Rect& b) { return {std::max(a.left, b.left), std::max(a.top, b.top), std::max(std::max(a.left, b.left), std::min(a.right, b.right)), std::max(std::max(a.top, b.top), std::min(a.bottom, b.bottom))}; }
// R34: the one rule for the layout the chat page gets. « bubble » only when the layout is Commentaire, the panel is
// not on screen (not even sliding out) and the bubble is open or kept warm; else « panel » (with its header, thread and
// composer at full height). Every `layout` the host sends goes through it: no race between the switch, the panel's
// slide and the warm bubble.
inline bool BubbleLayout(bool commentLayout, bool panelShown, bool bubbleOpen, bool bubbleWarm) { return commentLayout && !panelShown && (bubbleOpen || bubbleWarm); }
// The page an anchor belongs to: the URL without its fragment (a « #section » link does not change the page).
inline std::string PageKey(const std::string& url) { const auto hash = url.find('#'); return hash == std::string::npos ? url : url.substr(0, hash); }
// R39 (Q, 0.3.541): a scroll step is only a MOVE of the same window — same size, same inset (nothing told to the page, no
// new WebView size), the surface carried by exactly the element's move. The window kept for the opening (relative to the
// surface's fixed corner), else the window it has, carried. ok = false only when there is no window yet.
inline Kept ScrollFrame(const Rect& surface, bool fromRight, bool fromBottom, bool frameKept, const Rect& rel, const Edges& inset, int winW, int winH, float k, const Edges& over) {
    // monkey seed 2 #112: the window it has, moved as it is — the kept frame recomputed rounded the inset (537 -> 536)
    Kept out; if (winW > 0 && winH > 0) { out.ok = true; out.f = Reframe(surface, inset, winW, winH, k, over); return out; }
    if (frameKept) out = FrameKept(surface, fromRight, fromBottom, rel, k, over);
    return out;
}
// R39 (Q, 0.3.541 / 0.3.546): the follow thread waits for the next composition (DwmFlush), then the window moves. Which
// positions does that move take? F11 took those known when the thread WOKE (a position arriving during the wait waited a
// cycle more: 41 px behind with the wheel); 753b890 those known AT the composition — still one image late with the
// scrollbar (61 px = one step): the page's next position arrives right after the composition, before the window's
// message is handled. 04514f5 (measured green: bar 2/26) took the LATEST position known when the window moves.
enum class FollowAt { Wake, Composition, Move };
inline constexpr FollowAt FollowSample = FollowAt::Move;
// The follow pipeline, modelled (time in screen images, a composition at each whole number): `perFrame` positions arrive
// per image at `phase` (0..1) of their slot, the window handles the thread's message `handleDelay` after the composition.
// Returns, at the worst move, how many positions known when the window moved were not used by it.
inline int FollowBehind(FollowAt at, int perFrame, double handleDelay, int frames, double phase = .5) {
    auto when = [&](int f, int j) { return f + (j + phase) / perFrame; };
    auto arrived = [&](double t) { int n = 0; for (int f = 0; f < frames; ++f) for (int j = 0; j < perFrame; ++j) if (when(f, j) <= t) ++n; return n; };
    int worst = 0, placed = 0; double wake = when(0, 0);
    for (int guard = 0; wake < frames && guard < 10 * frames; ++guard) {
        const double vs = std::floor(wake) + 1;   // the thread's DwmFlush returns at the next composition
        const double handled = vs + handleDelay;
        const int gen = at == FollowAt::Wake ? arrived(wake) : at == FollowAt::Composition ? arrived(vs) : arrived(handled);
        worst = std::max(worst, arrived(handled) - gen); placed = gen;
        if (arrived(handled) > placed) { wake = handled; continue; }   // more positions: the next wait starts at once
        double next = -1; for (int f = 0; f < frames && next < 0; ++f) for (int j = 0; j < perFrame; ++j) if (when(f, j) > handled) { next = when(f, j); break; }
        if (next < 0) break; wake = next;
    }
    return worst;
}
// R39, prediction (coordinator: if one image of delay remains by construction): the element's next position from its
// speed — the last two positions on the page's own clock (t, ms) — `leadMs` ahead; never more than 1.5 of the last step,
// nothing when the steps are not regular (a stop, a first step) — the caller places the true position after a quiet moment.
inline int PredictShift(int y0, double t0, int y1, double t1, double leadMs) {
    const double dt = t1 - t0; if (!(dt >= 4 && dt <= 50) || leadMs <= 0) return 0;
    const double v = (y1 - y0) / dt; const double cap = 1.5 * std::abs(y1 - y0);
    return int(std::lround(std::clamp(v * leadMs, -cap, cap)));
}
// H2 (6e60162): « selecting » carries the viewport its rectangle was measured in (vw, vh, CSS px). A click right after the
// panel closed: the page still had its old width — that rectangle is stale; the bubble waits for the page's first measure
// (its first « anchor ») before it is shown. vw / vh 0: not said (an older page), trusted.
inline bool StaleViewport(double vw, double vh, double areaWcss, double areaHcss) {
    if (vw <= 0 || vh <= 0) return false;
    return std::abs(vw - areaWcss) > 2 || std::abs(vh - areaHcss) > 2;
}
// Monkey 0.3.561: the click's early opening (« selecting ») — no bubble open, or one open on another element (its bubble
// opens at the click); the open bubble's own element (or a bubble whose selector is unknown) waits for the full selection.
inline bool EarlyOpenAllowed(bool open, const std::string& openSelector, const std::string& clicked) {
    if (clicked.empty()) return false;
    return !open || (!openSelector.empty() && openSelector != clicked);
}
} // namespace ChatBubble
