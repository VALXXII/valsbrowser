#pragma once
// Annotation mode — pure host logic (V1): page message validation, crop planning, project
// choice by origin, small texts. No window, no WebView2, no I/O: unit-tested in
// tests/annotations/host_test.cpp. The integration (src/annotation-mode.h) feeds it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include <cwctype>
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

// ---------------------------------------------------------------- page messages
enum class Op { Invalid, Select, Hidden, Escape, NoSelection };
struct Envelope { Op op = Op::Invalid; std::int64_t serial = -1; };

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
    else if (op->string == "escape") e.op = Op::Escape;
    else if (op->string == "noselection") e.op = Op::NoSelection;
    else return bad("op");
    return e;
}

struct Selection {
    BA::Target target;
    std::string frameKind = "main";
    bool keyboard = false;
};

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

// Schema of a "select" message (types, bounds, allow-lists are then enforced by BA::Sanitize).
inline bool ParseSelection(const SJ::Value& v, Selection& out, std::string* why = nullptr) {
    using namespace detail; auto bad = [&](const char* w) { if (why) *why = w; return false; };
    const auto* t = v.Get("target"); const auto* vp = v.Get("viewport"); const auto* fr = v.Get("frame");
    if (!t || !t->IsObject() || !vp || !vp->IsObject() || !fr || !fr->IsObject()) return bad("shape");
    Selection s;
    if (!Str(fr->Get("kind"), s.frameKind, 16) || (s.frameKind != "main" && s.frameKind != "same-origin" && s.frameKind != "cross-origin")) return bad("frame.kind");
    if (const auto* k = v.Get("keyboard")) { if (!k->IsBool()) return bad("keyboard"); s.keyboard = k->boolean; }
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

// ---------------------------------------------------------------- origins and projects
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
inline int ProjectForOrigin(const std::vector<BA::Project>& projects, const std::string& origin) {
    for (std::size_t i = 0; i < projects.size(); ++i)
        for (const auto& o : projects[i].origins) if (o == origin) return int(i);
    return -1;
}
// Label from the page title's site part (« Paiement — Boutique » -> « Boutique »), else the host.
inline std::string LabelFor(const std::string& origin, const std::wstring& title) {
    std::wstring t = title;
    for (const wchar_t* sep : {L" — ", L" – ", L" | ", L" - ", L" · "}) { const auto at = t.rfind(sep); if (at != std::wstring::npos) { t = t.substr(at + wcslen(sep)); break; } }
    while (!t.empty() && iswspace(t.back())) t.pop_back();
    while (!t.empty() && iswspace(t.front())) t.erase(t.begin());
    if (t.size() >= 2 && t.size() <= 40 && t != title) return BA::CleanText(Utf8(t), BA::MaxLabelChars);
    const auto sep = origin.find("://"); return sep == std::string::npos ? origin : origin.substr(sep + 3);
}
inline std::string SlugFor(const std::string& label) {
    std::string s; bool dash = false;
    for (unsigned char c : label) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { if (dash && !s.empty()) s += '-'; s += char(c); dash = false; }
        else dash = true;
        if (s.size() >= 32) break;
    }
    if (s.empty() || !BA::IsValidProjectId(s)) s = "site";
    return s;
}
// A new project for this origin, id unique among `existing`.
inline BA::Project NewProjectFor(const std::string& origin, const std::wstring& title, const std::vector<BA::Project>& existing, bool hostLabel = false) {
    BA::Project p; p.label = hostLabel ? LabelFor(origin, L"") : LabelFor(origin, title);
    const std::string base = SlugFor(p.label); p.id = base;
    for (int n = 2; n < 1000; ++n) {
        const bool taken = std::any_of(existing.begin(), existing.end(), [&](const BA::Project& e) { return e.id == p.id; });
        if (!taken && BA::IsValidProjectId(p.id)) break;
        p.id = base.substr(0, 34) + "-" + std::to_string(n);
    }
    if (!origin.empty()) p.origins.push_back(origin);
    return p;
}

// ---------------------------------------------------------------- texts
inline std::wstring SinceText(std::int64_t seconds) {
    if (seconds < 60) return L"À l'instant";
    if (seconds < 3600) return L"Depuis " + std::to_wstring(seconds / 60) + L" min";
    if (seconds < 86400) return L"Depuis " + std::to_wstring(seconds / 3600) + L" h";
    return L"Depuis " + std::to_wstring(seconds / 86400) + L" j";
}
// Short form for the card header (« Il y a 3 min »): the long one does not fit beside the agent's name.
inline std::wstring AgoShort(std::int64_t seconds) {
    if (seconds < 60) return L"À l'instant";
    if (seconds < 3600) return L"Il y a " + std::to_wstring(seconds / 60) + L" min";
    if (seconds < 86400) return L"Il y a " + std::to_wstring(seconds / 3600) + L" h";
    return L"Il y a " + std::to_wstring(seconds / 86400) + L" j";
}
inline std::wstring AgoText(std::int64_t seconds) {
    if (seconds < 60) return L"il y a quelques secondes";
    if (seconds < 3600) return L"il y a " + std::to_wstring(seconds / 60) + L" min";
    if (seconds < 86400) return L"il y a " + std::to_wstring(seconds / 3600) + L" h";
    return L"il y a " + std::to_wstring(seconds / 86400) + L" j";
}
inline std::wstring Monogram(const std::wstring& name) {
    for (wchar_t c : name) if (iswalnum(c)) return std::wstring(1, wchar_t(towupper(c)));
    return L"A";
}
// Short, stable tag of a data directory (source.instance.store): FNV-1a of the lower-case path.
inline std::string StoreTag(const std::wstring& dataDir) {
    std::uint32_t h = 2166136261u;
    for (wchar_t c : dataDir) { h ^= std::uint32_t(towlower(c)); h *= 16777619u; }
    char b[9]; snprintf(b, sizeof b, "%06x", h & 0xFFFFFF); return b;
}
// What the popover's « Ce que l'agent verra » lists (page, element, text, styles).
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
    if (e.state.status == BA::Status::NeedsInfo) return L"Question de";
    if (e.state.status == BA::Status::Dismissed) return L"Écartée par";
    return L"Résolue par";
}
} // namespace AnnotationHost
