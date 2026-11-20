#pragma once
// Chat intégré — Mode Commentaire: where the bubble goes (pure, no Win32 window; tests/chat/chat_units_test.cpp).
// design/chat-integration.md « Mode Commentaire ». Paper 18NS-0 / 1AWI-0: the bubble sits in the free space next to the
// element it talks about — at its right if it fits, else at its left, else under it, else above it — and never leaves
// the page area. All rectangles in the same unit (screen pixels of the host).
#include <algorithm>
#include <string>

namespace ChatBubble {
struct Rect { int left = 0, top = 0, right = 0, bottom = 0; int W() const { return right - left; } int H() const { return bottom - top; } };
enum class Side { Right, Left, Below, Above, Inside };
struct Placement { Rect rect; Side side = Side::Right; };

inline int Clamp(int v, int lo, int hi) { return hi < lo ? lo : std::max(lo, std::min(v, hi)); }
// The bubble's size, bounded by the page area (minus the margins) and by the panel page's own maxima.
inline void Fit(int& w, int& h, const Rect& area, int margin, int maxW, int maxH) {
    w = std::max(1, std::min({w, maxW, area.W() - 2 * margin}));
    h = std::max(1, std::min({h, maxH, area.H() - 2 * margin}));
}
// element: the element's box; size: the bubble's (w, h); area: where it may go (the page's visible area); gap: between
// the element and the bubble; margin: from the area's edges. The bubble is aligned on the element's top (sides) or left
// (below / above), then slid to stay inside. When no side has room, it goes inside the area at the element's right edge
// (Inside: it may cover the element, never the window's edge).
// `lead`: how far the bubble starts before the element's top (sides) or left (below / above): Paper puts it 12 px from
// the element's right edge and 4 px above its top — on the 4 px selection frame drawn around the element.
// `prefer`: the side the open bubble is on — kept while it still fits, so a new height never makes it jump sides.
inline Placement Place(const Rect& element, int w, int h, const Rect& area, int gap, int margin, Side prefer = Side::Right, int lead = 0) {
    const int minX = area.left + margin, maxX = area.right - margin, minY = area.top + margin, maxY = area.bottom - margin;
    auto at = [&](int x, int y, Side s) { Placement p; p.side = s; x = Clamp(x, minX, maxX - w); y = Clamp(y, minY, maxY - h); p.rect = {x, y, x + w, y + h}; return p; };
    auto fits = [&](Side s) {
        switch (s) {
        case Side::Right: return element.right + gap + w <= maxX;
        case Side::Left: return element.left - gap - w >= minX;
        case Side::Below: return element.bottom + gap + h <= maxY;
        case Side::Above: return element.top - gap - h >= minY;
        default: return false;
        }
    };
    auto put = [&](Side s) {
        switch (s) {
        case Side::Right: return at(element.right + gap, element.top - lead, s);
        case Side::Left: return at(element.left - gap - w, element.top - lead, s);
        case Side::Below: return at(element.left - lead, element.bottom + gap, s);
        default: return at(element.left - lead, element.top - gap - h, Side::Above);   // above: its bottom stays on the element's top
        }
    };
    if (prefer != Side::Inside && fits(prefer)) return put(prefer);
    for (Side s : {Side::Right, Side::Left, Side::Below, Side::Above}) if (fits(s)) return put(s);
    return at(element.right - w, element.top, Side::Inside);
}
// R2: an element carries its frame + icon (Paper 18IJ-0) only once a message left its bubble — sent, waiting in the
// queue, or kept as an annotation (Ctrl+Entrée). A click on an element then closing its bubble leaves nothing on it.
inline bool SentItem(const std::string& type) { return type == "message.user" || type == "annotation.kept"; }
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
inline Framed FrameBubble(const Rect& surface, int maxH, bool bottomFixed, const Rect& screen, float k, const Edges& over, int marginCss = 400) {
    Rect full = surface;
    if (bottomFixed) full.top = std::min(surface.top, surface.bottom - maxH); else full.bottom = std::max(surface.bottom, surface.top + maxH);
    const Grown g = Grow(full, Edges{marginCss, marginCss, marginCss, marginCss}, screen, k);
    auto px = [&](int css) { return int(css * k + .5f); };
    auto css = [&](int p) { return k > 0 ? int(p / k) : 0; };
    Framed f; f.window = g.window;
    f.inset.left = css(surface.left - f.window.left); f.inset.top = css(surface.top - f.window.top);
    f.window.left = surface.left - px(f.inset.left); f.window.top = surface.top - px(f.inset.top);   // the surface exactly at the inset told
    f.inset.right = css(f.window.right - surface.right); f.inset.bottom = css(f.window.bottom - surface.bottom);
    const int sx = px(f.inset.left), sy = px(f.inset.top);
    f.region = {sx - px(std::min(over.left, f.inset.left)), sy - px(std::min(over.top, f.inset.top)),
                sx + surface.W() + px(std::min(over.right, f.inset.right)), sy + surface.H() + px(std::min(over.bottom, f.inset.bottom))};
    f.region.left = std::max(0, f.region.left); f.region.top = std::max(0, f.region.top);
    f.region.right = std::min(f.window.W(), f.region.right); f.region.bottom = std::min(f.window.H(), f.region.bottom);
    return f;
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
// permission or error only make it grow away from that edge; it is never moved to fit the window (the user scrolls).
// Only exception: at the end of the document, where the page cannot scroll further and the bubble would be cut, it
// moves up once by what is missing (`BottomShift`).
struct Anchor { bool set = false; Side side = Side::Right; int dx = 0, dy = 0; };   // dy: from the element's top to the fixed edge
inline Anchor AnchorOf(const Placement& p, const Rect& element) {
    Anchor a; a.set = true; a.side = p.side; a.dx = p.rect.left - element.left;
    a.dy = (p.side == Side::Above ? p.rect.bottom : p.rect.top) - element.top; return a;
}
inline Rect Anchored(const Anchor& a, const Rect& element, int w, int h) {
    Rect r; r.left = element.left + a.dx; r.right = r.left + w;
    if (a.side == Side::Above) { r.bottom = element.top + a.dy; r.top = r.bottom - h; } else { r.top = element.top + a.dy; r.bottom = r.top + h; }
    return r;
}
inline int BottomShift(const Rect& surface, const Rect& area, int margin, bool atDocumentEnd) {
    const int over = surface.bottom - (area.bottom - margin);
    return atDocumentEnd && over > 0 ? std::min(over, std::max(0, surface.top - (area.top + margin))) : 0;
}
// R29: what of the window may show — never over the browser's own bars (the page area, in window pixels).
inline Rect Clip(const Rect& a, const Rect& b) { return {std::max(a.left, b.left), std::max(a.top, b.top), std::max(std::max(a.left, b.left), std::min(a.right, b.right)), std::max(std::max(a.top, b.top), std::min(a.bottom, b.bottom))}; }
// R34: the one rule for the layout the chat page gets. « bubble » only when the layout is Commentaire, the panel is
// not on screen (not even sliding out) and the bubble is open or kept warm; else « panel » (with its header, thread and
// composer at full height). Every `layout` the host sends goes through it: no race between the switch, the panel's
// slide and the warm bubble.
inline bool BubbleLayout(bool commentLayout, bool panelShown, bool bubbleOpen, bool bubbleWarm) { return commentLayout && !panelShown && (bubbleOpen || bubbleWarm); }
// The page an anchor belongs to: the URL without its fragment (a « #section » link does not change the page).
inline std::string PageKey(const std::string& url) { const auto hash = url.find('#'); return hash == std::string::npos ? url : url.substr(0, hash); }
} // namespace ChatBubble
