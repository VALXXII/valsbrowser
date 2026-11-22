#pragma once
#include "i18n.h"
// Annotation mode — pure host logic (V2): page message validation, capture planning (engine clip and
// viewport crop), origins, small texts (element labels, ages, follow-up lines). No window, no WebView2, no I/O: unit-tested in
// tests/annotations/host_test.cpp. The integration (src/annotation-mode.h) feeds it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include <cctype>
#include <cwctype>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "annotations.h"
#include "strict-json.h"

namespace AnnotationHost {
namespace BA = BrowserAnnotations;
namespace SJ = StrictJson;

constexpr std::size_t MaxMessageBytes = 64 * 1024;   // one page message (UTF-8)

inline std::string Utf8(const std::wstring& s) {
    std::string o; o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); ++i; }
        else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        if (c < 0x80) o += char(c);
        else if (c < 0x800) { o += char(0xC0 | (c >> 6)); o += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += char(0xE0 | (c >> 12)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
        else { o += char(0xF0 | (c >> 18)); o += char(0x80 | ((c >> 12) & 0x3F)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
    }
    return o;
}
inline std::wstring Wide(const std::string& s) {
    std::wstring o; o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        std::uint32_t cp = 0; const std::size_t n = SJ::detail::Utf8Length(s, i, &cp);
        if (!n) { o += L'�'; ++i; continue; }
        if (cp >= 0x10000) { cp -= 0x10000; o += wchar_t(0xD800 + (cp >> 10)); o += wchar_t(0xDC00 + (cp & 0x3FF)); } else o += wchar_t(cp);
        i += n;
    }
    return o;
}

namespace detail {
inline bool Str(const SJ::Value* v, std::string& out, std::size_t maxBytes) { if (!v || !v->IsString() || v->string.size() > maxBytes) return false; out = v->string; return true; }
inline bool Num(const SJ::Value* v, double& out, double lo, double hi) { if (!v || !v->IsNumber() || !std::isfinite(v->number) || v->number < lo || v->number > hi) return false; out = v->number; return true; }
inline bool StrList(const SJ::Value* v, std::vector<std::string>& out, std::size_t maxItems, std::size_t maxBytes) {
    if (!v || !v->IsArray() || v->items.size() > maxItems) return false;
    for (const auto& i : v->items) { if (!i.IsString() || i.string.size() > maxBytes) return false; out.push_back(i.string); }
    return true;
}
inline bool Pairs(const SJ::Value* v, std::vector<BA::NamedValue>& out, std::size_t maxItems, bool allowNull) {
    if (!v || !v->IsObject() || v->members.size() > maxItems) return false;
    for (const auto& m : v->members) {
        BA::NamedValue nv; nv.name = m.first;
        if (m.first.size() > 64) return false;
        if (m.second.IsNull() && allowNull) nv.isNull = true;
        else if (m.second.IsString() && m.second.string.size() <= 4096) nv.value = m.second.string;
        else return false;
        out.push_back(std::move(nv));
    }
    return true;
}
}

// ---------------------------------------------------------------- page messages
enum class Op { Invalid, Select, Hidden, Shown, Escape, NoSelection, Anchor, Selecting, Mods };
// Anchor (chat, Mode Commentaire): where the element the bubble is attached to is now, CSS px of the top viewport;
// gone = it is no longer in the page.
// Selecting (latency): posted at the click, before the full « select » of the same seq: what was clicked —
// serial = seq, selector, x / y / w / h = the element's rectangle (CSS px of the top viewport), at = the click (ms since 1970).
// Anchor (R39): sx / sy = the page's scroll offset (CSS px), t = when the page measured it (ms since 1970, sub-ms):
// optional (0 when absent), for the host's frame-by-frame measure of the page -> host lag.
struct Envelope { Op op = Op::Invalid; std::int64_t serial = -1; double x = 0, y = 0, w = 0, h = 0, vw = 0, vh = 0; bool gone = false; std::string selector; double at = 0;
                  double sx = 0, sy = 0, t = 0;
                  double px = -1, py = -1; };   // Selecting: pt = where the page saw the click (CSS px of the top viewport); -1 when absent (diagnostics only)

// Checks the frame of every message: type/version, the session nonce and the navigation stamp.
// Anything else (unknown op, wrong types) is Invalid.
inline Envelope CheckEnvelope(const SJ::Value& v, const std::string& nonce, std::int64_t navStamp, std::string* why = nullptr) {
    Envelope e; auto bad = [&](const char* w) { if (why) *why = w; return Envelope{}; };
    if (!v.IsObject()) return bad("not an object");
    const auto* type = v.Get("type"); const auto* ver = v.Get("v"); const auto* n = v.Get("nonce"); const auto* st = v.Get("navStamp"); const auto* op = v.Get("op");
    if (!type || !type->IsString() || type->string != "browser-annotation") return bad("type");
    if (!ver || !ver->IsNumber() || !ver->integral || ver->integer != 1) return bad("version");
    if (!n || !n->IsString() || nonce.empty() || n->string != nonce) return bad("nonce");
    if (!st || !st->IsNumber() || !st->integral || st->integer != navStamp) return bad("navStamp");
    if (!op || !op->IsString()) return bad("op");
    if (op->string == "select") e.op = Op::Select;
    else if (op->string == "hidden") { e.op = Op::Hidden; if (const auto* s = v.Get("serial"); s && s->IsNumber() && s->integral) e.serial = s->integer; }
    else if (op->string == "shown") { e.op = Op::Shown; if (const auto* s = v.Get("serial"); s && s->IsNumber() && s->integral) e.serial = s->integer; }
    else if (op->string == "selecting") {
        const auto* q = v.Get("seq"); const auto* sel = v.Get("selector"); const auto* r = v.Get("rect");
        if (!q || !q->IsNumber() || !q->integral || q->integer < 0 || q->integer > (std::int64_t(1) << 40)) return bad("selecting seq");
        if (!sel || !sel->IsString() || sel->string.empty() || sel->string.size() > 4096) return bad("selecting selector");
        if (!r || !r->IsObject() || !detail::Num(r->Get("x"), e.x, -1e7, 1e7) || !detail::Num(r->Get("y"), e.y, -1e7, 1e7) ||
            !detail::Num(r->Get("width"), e.w, 0, 1e7) || !detail::Num(r->Get("height"), e.h, 0, 1e7)) return bad("selecting rect");
        if (const auto* t0 = v.Get("at")) { if (!detail::Num(t0, e.at, 0, 1e14)) return bad("selecting at"); }
        if (const auto* pt = v.Get("pt"); pt && pt->IsObject()) { double x = 0, y = 0; if (detail::Num(pt->Get("x"), x, -1e7, 1e7) && detail::Num(pt->Get("y"), y, -1e7, 1e7)) { e.px = x; e.py = y; } }   // optional
        e.op = Op::Selecting; e.serial = q->integer; e.selector = sel->string;
    }
    else if (op->string == "mods") e.op = Op::Mods;   // R31: the pointer moved over the page: the host reads Ctrl
    else if (op->string == "escape") e.op = Op::Escape;
    else if (op->string == "noselection") e.op = Op::NoSelection;
    else if (op->string == "anchor") {
        e.op = Op::Anchor;
        if (const auto* g = v.Get("gone"); g && g->IsBool() && g->boolean) { e.gone = true; return e; }
        auto num = [&](const char* k, double& out, double lo, double hi) { const auto* p = v.Get(k); if (!p || !p->IsNumber() || !(p->number >= lo && p->number <= hi)) return false; out = p->number; return true; };
        if (!num("x", e.x, -1e6, 1e6) || !num("y", e.y, -1e6, 1e6) || !num("w", e.w, 0, 1e6) || !num("h", e.h, 0, 1e6) || !num("vw", e.vw, 1, 1e5) || !num("vh", e.vh, 1, 1e5)) return bad("anchor");
        if (v.Get("sx") && !num("sx", e.sx, -1e7, 1e7)) return bad("anchor sx");
        if (v.Get("sy") && !num("sy", e.sy, -1e7, 1e7)) return bad("anchor sy");
        if (v.Get("t") && !num("t", e.t, 0, 1e14)) return bad("anchor t");
    }
    else return bad("op");
    return e;
}

// V3: the page frame's geometry at the selection (CSS px of the top viewport): edges (x, y, w, h), label box and
// text, fill opacity. The host draws the edges and the label natively (never captured) and removes the fill.
// V3-B: the spacing inspector as drawn by the page at the selection (CSS px of the top viewport): the padding ring
// (outer = padding box with its radius, inner = content box), the gap rectangles, the value labels, and the viewport
// (the hatches' common box). At most 96 gaps and 12 labels; anything malformed drops the inspector, never the selection.
struct SpaceBand { int kind = 1; double x = 0, y = 0, w = 0, h = 0; };
struct SpaceLabel { int kind = 0; double x = 0, y = 0, w = 0, h = 0; std::string text; };
struct Mark {
    bool present = false; double x = 0, y = 0, w = 0, h = 0, lx = 0, ly = 0, lw = 0, lh = 0, fill = 0; std::string text;
    double rx = 0, ry = 0, rw = 0, rh = 0; std::string rtext;   // R14: the size label at the right (absent: none)
    bool hasPad = false; double pad[8]{}; double radius = 0, innerRadius = 0, vw = 0, vh = 0;   // V3-B: padding ring, viewport
    std::vector<SpaceBand> bands; std::vector<SpaceLabel> labels;                                 // V3-B: gaps, value labels
    bool HasSpace() const { return hasPad || !bands.empty(); }
};
struct Selection {
    Mark mark;
    BA::Target target;
    std::string frameKind = "main";
    bool keyboard = false;
    std::int64_t seq = -1;   // the page's selection counter: its « hidden » message carries it (frame hidden for the shot)
    double at = 0;           // the page's Date.now() at the click (ms since 1970): click -> host latency, measured only
};


// V3-B: mark.sp — { pad: [ox, oy, ow, oh, ix, iy, iw, ih] | null, radius, inner, gaps: [[x, y, w, h]...],
// labels: [[kind, x, y, w, h, text]...], vw, vh }. False = ignore it.
inline bool ParseSpaceMark(const SJ::Value& sp, Mark& m) {
    using namespace detail; constexpr double lim = 1e6;
    if (!sp.IsObject()) return false;
    const auto* p = sp.Get("pad"); const auto* g = sp.Get("gaps"); const auto* l = sp.Get("labels");
    if (!g || !g->IsArray() || g->items.size() > 96 || !l || !l->IsArray() || l->items.size() > 12) return false;
    Mark out;
    if (p && !p->IsNull()) {
        if (!p->IsArray() || p->items.size() != 8) return false;
        for (size_t i = 0; i < 8; ++i) if (!Num(&p->items[i], out.pad[i], (i % 4) < 2 ? -lim : 0, lim)) return false;
        out.hasPad = true;
    }
    for (const auto& i : g->items) {
        if (!i.IsArray() || i.items.size() != 4) return false;
        SpaceBand x;
        if (!Num(&i.items[0], x.x, -lim, lim) || !Num(&i.items[1], x.y, -lim, lim) || !Num(&i.items[2], x.w, 0, lim) || !Num(&i.items[3], x.h, 0, lim)) return false;
        out.bands.push_back(x);
    }
    for (const auto& i : l->items) {
        if (!i.IsArray() || i.items.size() != 6) return false;
        SpaceLabel x; double k = 0;
        if (!Num(&i.items[0], k, 0, 1) || !Num(&i.items[1], x.x, -lim, lim) || !Num(&i.items[2], x.y, -lim, lim) || !Num(&i.items[3], x.w, 0, 400) || !Num(&i.items[4], x.h, 0, 100) ||
            !Str(&i.items[5], x.text, 16)) return false;
        x.kind = k >= .5 ? 1 : 0; out.labels.push_back(x);
    }
    if (const auto* r = sp.Get("radius")) { if (!Num(r, out.radius, 0, 10000)) return false; }
    if (const auto* r = sp.Get("inner")) { if (!Num(r, out.innerRadius, 0, 10000)) return false; }
    if (!Num(sp.Get("vw"), out.vw, 1, 100000) || !Num(sp.Get("vh"), out.vh, 1, 100000)) return false;
    m.hasPad = out.hasPad; std::copy(out.pad, out.pad + 8, m.pad); m.radius = out.radius; m.innerRadius = out.innerRadius; m.vw = out.vw; m.vh = out.vh;
    m.bands = std::move(out.bands); m.labels = std::move(out.labels); return true;
}
// V3-B: target.spacing from the page (tolerant: malformed => no spacing).
inline BA::Spacing ParseSpacing(const SJ::Value* v) {
    using namespace detail; BA::Spacing s;
    if (!v || !v->IsObject()) return s;
    const auto* p = v->Get("padding");
    if (!p || !p->IsArray() || p->items.size() != 4) return s;
    double pad[4];
    for (int i = 0; i < 4; ++i) if (!Num(&p->items[size_t(i)], pad[i], 0, 100000)) return s;
    s.top = pad[0]; s.right = pad[1]; s.bottom = pad[2]; s.left = pad[3];
    if (const auto* l = v->Get("layout"); l && l->IsString() && (l->string == "flex" || l->string == "grid")) s.layout = l->string;
    if (const auto* g = v->Get("gap"); g && g->IsObject() && !s.layout.empty()) {
        if (!Num(g->Get("row"), s.row, 0, 100000) || !Num(g->Get("column"), s.column, 0, 100000)) return BA::Spacing();
        s.hasGap = true;
    }
    s.present = true; return s;
}

// Schema of a "select" message (types, bounds, allow-lists are then enforced by BA::Sanitize).
inline bool ParseSelection(const SJ::Value& v, Selection& out, std::string* why = nullptr) {
    using namespace detail; auto bad = [&](const char* w) { if (why) *why = w; return false; };
    const auto* t = v.Get("target"); const auto* vp = v.Get("viewport"); const auto* fr = v.Get("frame");
    if (!t || !t->IsObject() || !vp || !vp->IsObject() || !fr || !fr->IsObject()) return bad("shape");
    Selection s;
    if (!Str(fr->Get("kind"), s.frameKind, 16) || (s.frameKind != "main" && s.frameKind != "same-origin" && s.frameKind != "cross-origin")) return bad("frame.kind");
    if (const auto* k = v.Get("keyboard")) { if (!k->IsBool()) return bad("keyboard"); s.keyboard = k->boolean; }
    if (const auto* t0 = v.Get("at")) { if (!t0->IsNumber() || !std::isfinite(t0->number) || t0->number < 0 || t0->number > 1e14) return bad("at"); s.at = t0->number; }
    if (const auto* q = v.Get("seq")) { if (!q->IsNumber() || !q->integral || q->integer < 0 || q->integer > (std::int64_t(1) << 40)) return bad("seq"); s.seq = q->integer; }
    if (const auto* mk = v.Get("mark")) {
        constexpr double lim = 1e6; auto& m = s.mark;
        if (!mk->IsObject() || !Num(mk->Get("x"), m.x, -lim, lim) || !Num(mk->Get("y"), m.y, -lim, lim) || !Num(mk->Get("w"), m.w, 0, lim) || !Num(mk->Get("h"), m.h, 0, lim) ||
            !Num(mk->Get("lx"), m.lx, -lim, lim) || !Num(mk->Get("ly"), m.ly, -lim, lim) || !Num(mk->Get("lw"), m.lw, 0, 4000) || !Num(mk->Get("lh"), m.lh, 0, 200) ||
            !Num(mk->Get("fill"), m.fill, 0, 1) || !Str(mk->Get("text"), m.text, 1024)) return bad("mark");
        m.present = true;
        if (mk->Get("rw")) {   // R14: optional; malformed drops the size label only
            if (!Num(mk->Get("rx"), m.rx, -lim, lim) || !Num(mk->Get("ry"), m.ry, -lim, lim) || !Num(mk->Get("rw"), m.rw, 0, 4000) || !Num(mk->Get("rh"), m.rh, 0, 200) || !Str(mk->Get("rtext"), m.rtext, 160)) { m.rw = m.rh = 0; m.rtext.clear(); }
        }
        if (const auto* sp = mk->Get("sp"); sp && !sp->IsNull() && !ParseSpaceMark(*sp, m)) { m.hasPad = false; m.bands.clear(); m.labels.clear(); }
    }
    auto& g = s.target;
    if (!Str(t->Get("kind"), g.kind, 16) || (g.kind != "element" && g.kind != "selection")) return bad("target.kind");
    if (!Str(t->Get("selector"), g.selector, 4096)) return bad("selector");
    const auto* uq = t->Get("selectorUnique"); if (!uq || !uq->IsBool()) return bad("selectorUnique"); g.selectorUnique = uq->boolean;
    if (!StrList(t->Get("path"), g.path, 64, 1024)) return bad("path");
    if (!StrList(t->Get("shadowPath"), g.shadowPath, 64, 1024)) return bad("shadowPath");
    if (!Str(t->Get("tag"), g.tag, 64) || g.tag.empty()) return bad("tag");
    if (!Str(t->Get("role"), g.role, 128)) return bad("role");
    if (!Str(t->Get("accessibleName"), g.accessibleName, 4096)) return bad("accessibleName");
    if (!Pairs(t->Get("attributes"), g.attributes, 32, true)) return bad("attributes");
    if (!Str(t->Get("text"), g.text, 4 * BA::MaxTextChars * 2)) return bad("text");
    const auto* sel = t->Get("selectionText");
    if (!sel) return bad("selectionText");
    if (sel->IsString()) { if (sel->string.size() > 4 * BA::MaxTextChars * 2) return bad("selectionText"); g.selectionText = sel->string; }
    else if (!sel->IsNull()) return bad("selectionText");
    if ((g.kind == "selection") != g.selectionText.has_value()) return bad("selection kind");
    const auto* r = t->Get("rect"); if (!r || !r->IsObject()) return bad("rect");
    constexpr double big = 1e7;
    if (!Num(r->Get("x"), g.rect.x, -big, big) || !Num(r->Get("y"), g.rect.y, -big, big) || !Num(r->Get("width"), g.rect.width, 0, big) || !Num(r->Get("height"), g.rect.height, 0, big)) return bad("rect values");
    if (!Pairs(t->Get("styles"), g.styles, 64, false)) return bad("styles");
    if (!Pairs(t->Get("sourceAttributes"), g.sourceAttributes, 16, false)) return bad("sourceAttributes");
    if (g.kind == "element") g.spacing = ParseSpacing(t->Get("spacing"));
    if (!Num(vp->Get("width"), g.viewport.width, 1, 100000) || !Num(vp->Get("height"), g.viewport.height, 1, 100000) ||
        !Num(vp->Get("scrollX"), g.viewport.scrollX, -big, big) || !Num(vp->Get("scrollY"), g.viewport.scrollY, -big, big) ||
        !Num(vp->Get("devicePixelRatio"), g.viewport.devicePixelRatio, 0.05, 20)) return bad("viewport");
    out = std::move(s); return true;
}

// The whole check of a raw page message (JSON text from get_WebMessageAsJson).
struct Checked { Envelope env; Selection selection; std::string why; };
inline Checked CheckMessage(const std::string& json, const std::string& nonce, std::int64_t navStamp) {
    Checked c;
    if (json.size() > MaxMessageBytes) { c.why = "too large"; return c; }
    SJ::Value v; SJ::Limits lim; lim.maxBytes = MaxMessageBytes; lim.maxDepth = 8; lim.maxStringBytes = 32 * 1024; lim.maxItems = 1024;
    if (!SJ::Parse(json, v, &c.why, lim)) return c;
    c.env = CheckEnvelope(v, nonce, navStamp, &c.why);
    if (c.env.op == Op::Select && !ParseSelection(v, c.selection, &c.why)) c.env.op = Op::Invalid;
    return c;
}
// Cheap prefix test before any parsing: only our messages are taken out of the shared router.
inline bool LooksLikeAnnotation(const std::wstring& json) {
    return json.size() > 2 && json.find(L"\"type\":\"browser-annotation\"") != std::wstring::npos;
}

// R26: the argument of the page command « peek » (annotation-script.h, peekSet): the chip's element shown as hovered
// (scrolled softly into view when it is not), or the end of it. {"active":true,"selector":"…"} / {"active":false}.
inline std::string PeekArg(const std::string& selector, bool active) {
    SJ::Value v = SJ::Value::Obj();
    const bool on = active && !selector.empty() && selector.size() <= 4096;
    v.Set("active", SJ::Value::Bool(on));
    if (on) v.Set("selector", SJ::Value::Str(selector));
    return SJ::Serialize(v);
}

// Review M4 (b): Escape from the page (the keyboard is in the site: after a click on its scroll bar, back from another
// application). A bubble open = it closes, the mode stays; otherwise the mode is left (unless it is paused: nothing).
enum class EscapeAction { CloseBubble, Leave, Ignore };
inline EscapeAction OnEscape(bool paused, bool bubbleOpen) { return bubbleOpen ? EscapeAction::CloseBubble : paused ? EscapeAction::Ignore : EscapeAction::Leave; }

// ---------------------------------------------------------------- capture crop
// rect/viewport in CSS px of the top viewport; capture = the CapturePreview PNG (viewport pixels).
struct CropPlan {
    bool ok = false;
    int x = 0, y = 0, w = 0, h = 0;        // crop in capture pixels
    unsigned outW = 0, outH = 0;           // after reduction (<= maxSide)
    double scale = 1;                      // outW / w
    double markX = 0, markY = 0, markW = 0, markH = 0;  // the element inside the crop (crop pixels)
    std::string crop;                      // "element+margin" | "element+margin (visible part)"
};
inline CropPlan PlanCrop(const BA::Rect& r, double viewW, double viewH, unsigned capW, unsigned capH, double marginCss = 24, unsigned maxSide = BA::MaxShotSide) {
    CropPlan p;
    if (!(viewW > 0) || !(viewH > 0) || !capW || !capH || !std::isfinite(r.x) || !std::isfinite(r.y)) return p;
    // CSS px -> capture px (DPR x zoom), measured on the width only: the band under the toolbar may change
    // the viewport height between the page's measure and the shot, never its width or scale.
    const double sx = capW / viewW, sy = sx; (void)viewH;
    const double ex0 = r.x * sx, ey0 = r.y * sy, ex1 = (r.x + std::max(0.0, r.width)) * sx, ey1 = (r.y + std::max(0.0, r.height)) * sy;
    double x0 = ex0 - marginCss * sx, y0 = ey0 - marginCss * sy, x1 = ex1 + marginCss * sx, y1 = ey1 + marginCss * sy;
    const bool clipped = x0 < 0 || y0 < 0 || x1 > capW || y1 > capH;
    x0 = std::clamp(x0, 0.0, double(capW)); y0 = std::clamp(y0, 0.0, double(capH)); x1 = std::clamp(x1, 0.0, double(capW)); y1 = std::clamp(y1, 0.0, double(capH));
    p.x = int(std::floor(x0)); p.y = int(std::floor(y0)); p.w = int(std::ceil(x1)) - p.x; p.h = int(std::ceil(y1)) - p.y;
    if (p.w < 2 || p.h < 2) return p;   // nothing visible
    const double longest = std::max(p.w, p.h);
    p.scale = longest > maxSide ? maxSide / longest : 1.0;
    p.outW = std::max(1u, unsigned(std::lround(p.w * p.scale))); p.outH = std::max(1u, unsigned(std::lround(p.h * p.scale)));
    p.outW = std::min(p.outW, maxSide); p.outH = std::min(p.outH, maxSide);
    p.markX = std::max(0.0, ex0 - p.x); p.markY = std::max(0.0, ey0 - p.y);
    p.markW = std::min(double(p.w), ex1 - p.x) - p.markX; p.markH = std::min(double(p.h), ey1 - p.y) - p.markY;
    p.markW = std::max(0.0, p.markW); p.markH = std::max(0.0, p.markH);
    const bool elementClipped = ex0 < 0 || ey0 < 0 || ex1 > capW || ey1 > capH;
    p.crop = elementClipped ? "element+margin (visible part)" : clipped ? "element+margin (viewport edge)" : "element+margin";
    p.ok = true; return p;
}

// Engine clip for Page.captureScreenshot (CDP): element + margin in CSS px of the DOCUMENT (scroll included),
// limited to the visible viewport (no capture beyond it: fast, and what the user saw). `scale` is the CDP
// output scale so that the longest side stays <= maxSide device pixels (DPR includes the browser zoom).
// mark* = the element inside the clip, in CSS px relative to the clip's top-left corner.
struct ClipPlan {
    bool ok = false;
    double x = 0, y = 0, width = 0, height = 0;   // document CSS px
    double scale = 1;
    unsigned outW = 0, outH = 0;                  // expected image size (device px x scale)
    double markX = 0, markY = 0, markW = 0, markH = 0;
    double viewX = 0, viewY = 0;                  // the clip's origin in CSS px of the viewport (V3: fill removal)
    double scrollX = 0, scrollY = 0, dpr = 1;     // V3: the engine snaps a fractional scroll to device px (fill removal)
    std::string crop;
};
inline ClipPlan PlanClip(const BA::Rect& r, const BA::Viewport& vp, double marginCss = 24, unsigned maxSide = BA::MaxShotSide) {
    ClipPlan p;
    if (!(vp.width > 0) || !(vp.height > 0) || !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(vp.scrollX) || !std::isfinite(vp.scrollY)) return p;
    const double dpr = vp.devicePixelRatio > 0 && std::isfinite(vp.devicePixelRatio) ? vp.devicePixelRatio : 1;
    const double ex0 = r.x, ey0 = r.y, ex1 = r.x + std::max(0.0, r.width), ey1 = r.y + std::max(0.0, r.height);
    double x0 = ex0 - marginCss, y0 = ey0 - marginCss, x1 = ex1 + marginCss, y1 = ey1 + marginCss;
    const bool clipped = x0 < 0 || y0 < 0 || x1 > vp.width || y1 > vp.height;
    x0 = std::clamp(x0, 0.0, vp.width); y0 = std::clamp(y0, 0.0, vp.height); x1 = std::clamp(x1, 0.0, vp.width); y1 = std::clamp(y1, 0.0, vp.height);
    // Whole CSS pixels: the engine rounds the clip anyway, the mark must agree with the image.
    x0 = std::floor(x0); y0 = std::floor(y0); x1 = std::ceil(x1); y1 = std::ceil(y1);
    if (x1 - x0 < 2 || y1 - y0 < 2) return p;
    p.width = x1 - x0; p.height = y1 - y0;
    const double longest = std::max(p.width, p.height) * dpr;
    p.scale = longest > maxSide ? maxSide / longest : 1.0;
    p.outW = std::min<unsigned>(maxSide, std::max(1u, unsigned(std::lround(p.width * dpr * p.scale))));
    p.outH = std::min<unsigned>(maxSide, std::max(1u, unsigned(std::lround(p.height * dpr * p.scale))));
    p.x = x0 + vp.scrollX; p.y = y0 + vp.scrollY; p.viewX = x0; p.viewY = y0; p.scrollX = vp.scrollX; p.scrollY = vp.scrollY; p.dpr = dpr;
    p.markX = std::max(0.0, ex0 - x0); p.markY = std::max(0.0, ey0 - y0);
    p.markW = std::max(0.0, std::min(p.width, ex1 - x0) - p.markX); p.markH = std::max(0.0, std::min(p.height, ey1 - y0) - p.markY);
    const bool elementClipped = ex0 < 0 || ey0 < 0 || ex1 > vp.width || ey1 > vp.height;
    p.crop = elementClipped ? "element+margin (visible part)" : clipped ? "element+margin (viewport edge)" : "element+margin";
    p.ok = true; return p;
}
// V3: the whole viewport as on screen (no clip: a clipped shot re-renders the view on screen, see annotation-capture.h).
inline std::string ViewportShotParams() { return "{\"format\":\"png\",\"fromSurface\":true,\"optimizeForSpeed\":true,\"captureBeyondViewport\":false}"; }
// The CDP parameters (JSON) for a plan: PNG, the clip, no capture beyond the viewport, encoder tuned for speed.
inline std::string ClipParams(const ClipPlan& p) {
    SJ::Value clip = SJ::Value::Obj();
    clip.Set("x", SJ::Value::Num(p.x)); clip.Set("y", SJ::Value::Num(p.y)); clip.Set("width", SJ::Value::Num(p.width)); clip.Set("height", SJ::Value::Num(p.height)); clip.Set("scale", SJ::Value::Num(p.scale));
    SJ::Value v = SJ::Value::Obj();
    v.Set("format", SJ::Value::Str("png")); v.Set("clip", std::move(clip)); v.Set("captureBeyondViewport", SJ::Value::Bool(false));
    v.Set("fromSurface", SJ::Value::Bool(true)); v.Set("optimizeForSpeed", SJ::Value::Bool(true));
    return SJ::Serialize(v);
}
// {"data":"<base64>"} from Page.captureScreenshot -> PNG bytes (empty on any error).
inline bool Base64Decode(const std::string& in, std::string& out) {
    static const auto table = [] { std::array<signed char, 256> t{}; t.fill(-1); const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; ++i) t[static_cast<unsigned char>(a[i])] = static_cast<signed char>(i); return t; }();
    out.clear(); out.reserve(in.size() / 4 * 3);
    std::uint32_t acc = 0; int bits = 0; std::size_t pad = 0;
    for (char ch : in) {
        if (ch == '=') { ++pad; continue; }
        if (pad) return false;   // data after padding
        const int d = table[static_cast<unsigned char>(ch)]; if (d < 0) return false;
        acc = (acc << 6) | std::uint32_t(d); bits += 6;
        if (bits >= 8) { bits -= 8; out += char((acc >> bits) & 0xFF); }
    }
    return pad <= 2 && (in.size() % 4) == 0;
}
inline bool ScreenshotData(const std::string& json, std::string& png) {
    SJ::Value v; SJ::Limits lim; lim.maxBytes = 12u << 20; lim.maxDepth = 4; lim.maxStringBytes = 12u << 20; lim.maxItems = 16;
    if (!SJ::Parse(json, v, nullptr, lim) || !v.IsObject()) return false;
    const auto* d = v.Get("data"); if (!d || !d->IsString() || d->string.empty()) return false;
    return Base64Decode(d->string, png) && png.size() > 8 && png.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
}

// ---------------------------------------------------------------- origins
// "scheme://host[:port]" in lower case for http(s); empty for anything else.
inline std::string OriginOf(const std::wstring& url) {
    const std::string u = Utf8(url); const auto sep = u.find("://"); if (sep == std::string::npos) return {};
    std::string scheme = u.substr(0, sep); for (auto& c : scheme) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (scheme != "http" && scheme != "https") return {};
    std::size_t end = u.find_first_of("/?#", sep + 3); std::string auth = u.substr(sep + 3, end == std::string::npos ? std::string::npos : end - sep - 3);
    if (const auto at = auth.rfind('@'); at != std::string::npos) auth = auth.substr(at + 1);
    for (auto& c : auth) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (auth.empty() || auth.size() > 255) return {};
    if (scheme == "http" && auth.size() > 3 && auth.compare(auth.size() - 3, 3, ":80") == 0) auth.resize(auth.size() - 3);
    if (scheme == "https" && auth.size() > 4 && auth.compare(auth.size() - 4, 4, ":443") == 0) auth.resize(auth.size() - 4);
    return scheme + "://" + auth;
}
// ---------------------------------------------------------------- texts
inline std::wstring SinceText(std::int64_t seconds) {
    if (seconds < 60) return TR(age_now_cap);
    if (seconds < 3600) return TRF(since_minutes_cap, std::to_wstring(seconds / 60));
    if (seconds < 86400) return TRF(since_hours_cap, std::to_wstring(seconds / 3600));
    return TRF(since_days_cap, std::to_wstring(seconds / 86400));
}
// Short form for the card header (« Il y a 3 min »): the long one does not fit beside the agent's name.
inline std::wstring AgoShort(std::int64_t seconds) {
    if (seconds < 60) return TR(age_now_cap);
    if (seconds < 3600) return TRF(ago_minutes_cap, std::to_wstring(seconds / 60));
    if (seconds < 86400) return TRF(ago_hours_cap, std::to_wstring(seconds / 3600));
    return TRF(ago_days_short_cap, std::to_wstring(seconds / 86400));
}
inline std::wstring AgoText(std::int64_t seconds) {
    if (seconds < 60) return TR(ago_seconds);
    if (seconds < 3600) return TRF(ago_minutes, std::to_wstring(seconds / 60));
    if (seconds < 86400) return TRF(ago_hours, std::to_wstring(seconds / 3600));
    return TRF(ago_days_short, std::to_wstring(seconds / 86400));
}
inline std::wstring Monogram(const std::wstring& name) {
    for (wchar_t c : name) if (iswalnum(c)) return std::wstring(1, wchar_t(towupper(c)));
    return L"A";
}
// Card 3.2 « Claude s'en occupe · depuis 2 min » (lower case: it follows the title).
inline std::wstring SinceLower(std::int64_t seconds) {
    if (seconds < 60) return TR(ago_now);
    if (seconds < 3600) return TRF(since_minutes, std::to_wstring(seconds / 60));
    if (seconds < 86400) return TRF(since_hours, std::to_wstring(seconds / 3600));
    return TRF(since_days, std::to_wstring(seconds / 86400));
}
// History row age, right-aligned: « 2 min », « 3 h », « Hier », a weekday within the week, else « 12 nov. ».
// `localDays` = whole local days between the two dates (0 = same day); weekday 0 = dimanche.
inline std::wstring AgeLabel(std::int64_t seconds, int localDays, int weekday, int day, int month) {
    using I = I18n::Id;
    static const I days[] = {I::weekday_0, I::weekday_1, I::weekday_2, I::weekday_3, I::weekday_4, I::weekday_5, I::weekday_6};
    static const I months[] = {I::month_short_1, I::month_short_2, I::month_short_3, I::month_short_4, I::month_short_5, I::month_short_6,
                               I::month_short_7, I::month_short_8, I::month_short_9, I::month_short_10, I::month_short_11, I::month_short_12};
    if (seconds < 60) return TR(age_now_cap);
    if (localDays <= 0 && seconds < 3600) return TRF(age_minutes, std::to_wstring(seconds / 60));
    if (localDays <= 0) return TRF(age_hours, std::to_wstring(seconds / 3600));
    if (localDays == 1) return TR(age_yesterday_cap);
    if (localDays < 7 && weekday >= 0 && weekday < 7) return I18n::T(days[weekday]);
    return TRF(history_date, month >= 1 && month <= 12 ? std::wstring(I18n::T(months[month - 1])) : std::wstring(), std::to_wstring(day));
}
// What the agent is called on cards and rows: « Claude », « Codex », else its own label (clipped).
inline std::wstring AgentDisplay(const std::string& agent) {
    std::string low; for (char c : agent) low += char(std::tolower(static_cast<unsigned char>(c)));
    if (low.find("claude") != std::string::npos) return L"Claude";
    if (low.find("codex") != std::string::npos) return L"Codex";
    std::wstring w = Wide(agent); if (w.empty()) return TR(agent_someone);
    if (w.size() > 24) { w.resize(23); w += L"…"; }
    return w;
}
inline int AgentKindOf(const std::string& agent) {   // 0 Claude, 1 Codex, 2 other (AUI::AgentKind)
    std::string low; for (char c : agent) low += char(std::tolower(static_cast<unsigned char>(c)));
    return low.find("claude") != std::string::npos ? 0 : low.find("codex") != std::string::npos ? 1 : 2;
}
// The element in words for « Ce que l'agent verra » (2.2): « Bouton « Payer 48,00 € » ».
inline std::wstring ElementLabel(const BA::Target& t) {
    const std::string tag = t.tag, role = t.role;
    std::wstring noun;
    if (t.kind == "selection") noun = TR(element_selection);
    else if (role == "button" || tag == "button") noun = TR(element_button);
    else if (role == "link" || tag == "a") noun = TR(element_link);
    else if (tag == "input" || tag == "textarea" || role == "textbox") noun = TR(element_field);
    else if (tag == "select" || role == "combobox" || role == "listbox") noun = TR(element_list);
    else if (tag == "img" || role == "img") noun = TR(element_image);
    else if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') noun = TR(element_heading);
    else if (tag == "p") noun = TR(element_paragraph);
    else if (tag == "li") noun = TR(element_list_item);
    else if (tag == "ul" || tag == "ol") noun = TR(element_list);
    else if (tag == "nav") noun = TR(a11y_toolbar);
    else if (tag == "form") noun = TR(element_form);
    else if (tag == "table") noun = TR(element_table);
    else if (tag == "canvas") noun = TR(element_canvas);
    else if (tag == "iframe" || tag == "frame") noun = TR(element_frame);
    else if (tag == "svg") noun = TR(element_illustration);
    else if (tag == "label") noun = TR(element_label);
    else if (tag == "header") noun = TR(element_header);
    else if (tag == "footer") noun = TR(element_footer);
    else if (tag == "section" || tag == "article" || tag == "main" || tag == "aside") noun = TR(element_section);
    else noun = TRF(element_tag, Wide(tag));
    std::wstring name = Wide(t.kind == "selection" && t.selectionText ? *t.selectionText : !t.accessibleName.empty() ? t.accessibleName : t.text);
    for (auto& c : name) if (c == L'\n' || c == L'\r' || c == L'\t') c = L' ';
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    if (name.size() > 40) { name.resize(39); while (!name.empty() && iswspace(name.back())) name.pop_back(); name += L"…"; }
    return name.empty() ? noun : TRF(element_named, noun, name);
}
// Short, stable tag of a data directory (source.instance.store): FNV-1a of the lower-case path.
inline std::string StoreTag(const std::wstring& dataDir) {
    std::uint32_t h = 2166136261u;
    for (wchar_t c : dataDir) { h ^= std::uint32_t(towlower(c)); h *= 16777619u; }
    char b[9]; snprintf(b, sizeof b, "%06x", h & 0xFFFFFF); return b;
}
// What the popover's « Ce que l'agent verra » lists (page, element, text, styles).
// V3-B: the note panel's « Style » row: padding / gap first when the element has them.
inline std::wstring StyleSummary(const BA::Target& t) {
    const std::string sp = BA::SpacingText(t.spacing);
    if (!sp.empty()) return Wide(sp);
    return t.styles.empty() ? std::wstring() : std::wstring(TR(note_style_summary));
}
inline std::wstring StylesText(const std::vector<BA::NamedValue>& styles) {
    static const char* keep[] = {"display", "position", "width", "height", "margin", "padding", "font-size", "font-weight", "color", "background-color", "border-radius", "text-align"};
    std::wstring o; int n = 0;
    for (const char* k : keep) for (const auto& s : styles) if (s.name == k && n < 6) { if (n++) o += L"\n"; o += Wide(s.name) + L": " + Wide(s.value); }
    return o;
}

// ---------------------------------------------------------------- agent status
struct AgentStatus {
    int claimed = 0;                       // claimed annotations (badge)
    std::optional<BA::Entry> working;      // most recently claimed
    std::optional<BA::Entry> resolved;     // most recent resolution/question/dismissal newer than `since`
    std::int64_t claimedAt = 0;
};
inline std::int64_t ClaimTime(const BA::Entry& e) {
    for (auto it = e.state.events.rbegin(); it != e.state.events.rend(); ++it) if (it->to == "claimed") return it->at;
    return e.state.updatedAt;
}
inline AgentStatus Summarize(const std::vector<BA::Entry>& entries, std::int64_t resolvedSince) {
    AgentStatus s;
    for (const auto& e : entries) {
        if (e.draft) continue;
        if (e.state.status == BA::Status::Claimed) {
            ++s.claimed; const auto at = ClaimTime(e);
            if (!s.working || at > s.claimedAt) { s.working = e; s.claimedAt = at; }
        } else if (e.state.resolution && e.state.resolution->at >= resolvedSince &&
                   (e.state.status == BA::Status::Resolved || e.state.status == BA::Status::NeedsInfo || e.state.status == BA::Status::Dismissed)) {
            if (!s.resolved || e.state.resolution->at > s.resolved->state.resolution->at) s.resolved = e;
        }
    }
    return s;
}
inline std::wstring OutcomeHeadline(const BA::Entry& e) {
    if (e.state.status == BA::Status::NeedsInfo) return TR(outcome_question_from);
    if (e.state.status == BA::Status::Dismissed) return TR(outcome_dismissed_by);
    return TR(outcome_resolved_by);
}
} // namespace AnnotationHost
