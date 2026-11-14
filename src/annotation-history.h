#pragma once
// History panel (V2) — Paper « MCP - Design UI UX »: 4.1 10Z6-0 (ouvert depuis le lanceur), 4.2 114Q-0
// (détail), 4.3 115M-0 (vide); rules + motion « Spéc · Historique » 1167-0.
//   Panel 360 at right 16 / bottom 64 of the page (8 above the launcher), canvas #171717, border
//   1 #FFFFFF14, radius 12.
//   Header padding 14/10/10/16, gap 8: history glyph 16 at 50 %, « Historique » 16/20 600 white,
//     close 26 (glyph 14 at 70 %).
//   Filters (margin 0 12 6): raised #292929 radius 8 padding 3 gap 4; segments flex 1, h 26,
//     radius 6; chosen #3E3E3E + 12/16 medium white; others 12/16 medium #FFFFFFB3 + count 11/14
//     tabular #FFFFFF80 (gap 5): Toutes / En cours n / Terminées n.
//   List: border-top #FFFFFF0F, padding 0 4 4, internal scroll (overscroll contained).
//     Section (padding 10/10/4, gap 6): site 12/16 medium #FFFFFFB3, « · cette page » 11/14
//     #FFFFFF80 (the current page's site comes first), count 11/14 tabular #FFFFFF80 at right.
//     Row (radius 8, padding 8, gap 10; hovered / selected #FFFFFF0A): capture 44x32 radius 5
//     (outline #FFFFFF1A); note 13/18 medium white on one line with an ellipsis; status 12/16
//     #FFFFFF99 (agent logo 12 + name / dot 6 #FFFFFF66 « En attente » / check 12 at 70 %
//     « Terminée par … »), age 12/16 #FFFFFF99 at right; « … » 24 (#FFFFFF14, radius 5, dots
//     14) on hover / focus, its slot kept in every row; done rows at 70 %.
//   Detail (4.2): header padding 12/10/10, gap 6: back 26 (caret-left 14 at 80 %), page 13/18
//     #FFFFFFB3, close 26; capture 150 high, margin 0 12, radius 8; note 14/20 #FFFFFFE6
//     (padding 12/16/0); follow-up box (margin 12 12 0, raised, radius 8, padding 10/12, gap 6):
//     logo 14 + status 13/18 medium white + age 12/16 #FFFFFF99 at right, then 12/17 #FFFFFF99;
//     actions padding 12, gap 8, h 30: « Copier en Markdown » raised 12/16 medium white,
//     « Supprimer » danger 10 % with #FF7B6E (subdued red).
//     V3-B (Paper 5.5): an annotation an agent gave back (« … n'a pas pu la traiter ») gets « Rouvrir » first
//     (raised, hugging its text, padding 10), then « Copier en Markdown » (hugging), « Supprimer » hugging at the
//     right. « Rouvrir » puts it back in waiting for an agent (the host: state open, « Rouverte » in the follow-up).
//   Empty (4.3): body padding 20/24, gap 6: the mark 22 at 50 % crossed by an x 20 #8B8B8B (canvas
//     halo); « Aucune annotation pour l'instant... » 14/20 medium #FFFFFF80; « Annoter cette
//     page » (accent, h 32, padding 12, key caps Ctrl Maj A #00000038, gap 2).
//   Toast « Annuler » (6 s, one-shot, paused under the pointer / focus): left of the launcher on
//     its line, h 40, canvas, border #FFFFFF14, radius 10: « Annotation supprimée » 13/16
//     #FFFFFFE6 + « Annuler » raised h 28 padding 10 13/16 medium.
// Motion: the panel is born from the launcher (origin bottom-right, 0.96 -> 1 + opacity, 200 ms
// cubic-bezier(.23,1,.32,1)) and returns there in 150 ms; the detail slides in from the right in a
// cross-fade (0.3 s critical spring), the height follows in 200 ms. Reduced motion: fades only.
// Data in, callbacks out: the host reads its store when the panel opens (and watches it only
// while it is open), deletes on remove(), restores on undo(), forgets on removeFinal().
#include "annotation-kit.h"

namespace NativeChrome { namespace Annotations {
enum class EntryStatus { Pending = 0, Working = 1, Done = 2 };
struct HistoryEntry {
    std::wstring id, site, page, note;      // site « localhost:5173 », page « localhost:5173/checkout »
    EntryStatus status = EntryStatus::Pending;
    AgentKind agent = AgentKind::Claude; std::wstring agentName;   // « Claude »
    std::wstring age;                       // « 2 min », « Hier », « Mardi »
    std::wstring followUp;                  // « Ajoutée à 14:02 · prise en charge à 14:04 »
    std::wstring summary;                   // the agent's summary when done
    Shot thumb;                             // row capture (cover-fitted 44x32), optional
    Shot capture;                           // detail capture (V3: contain-fitted inside 334x150), optional (else thumb)
    // V3 (Paper 5.5 « Suivi par l'agent »): what the agents did, even when they stopped on the way.
    std::wstring readBy;                    // « Claude »: an agent read it (bridge readers, per annotation) — « Lue par Claude »
    std::wstring readAge;                   // « il y a 2 min »
    bool unhandled = false;                 // an agent gave it back with a reason: « Claude n'a pas pu la traiter » (amber), still « En cours »
    std::wstring reason;                    // that reason, quoted in the detail
    struct Step { enum Kind { Added, Read, Claimed, Released, Unhandled, Done, Question, Dismissed, Reopened } kind = Added;
                  AgentKind agent = AgentKind::Other; std::wstring label, time, quote; };
    std::vector<Step> steps;                // the detail's « Suivi », oldest first (empty: followUp as plain text)
};
inline constexpr const wchar_t* UnhandledHelp = L"Pour qu'un agent modifie le site, ouvrez Claude Code ou Codex dans le dossier du projet, puis rouvrez l'annotation.";
struct HistoryData { std::wstring currentSite; std::vector<HistoryEntry> entries; };   // most recent first
enum class HistoryFilter { All = 0, Open = 1, Done = 2 };
struct HistoryHooks {
    std::function<void(const std::wstring& id)> remove;       // immediate (the row is gone; « Annuler » for 6 s)
    std::function<void(const std::wstring& id)> undo;         // « Annuler »: restore it
    std::function<void(const std::wstring& id)> removeFinal;  // the toast expired: forget it for good
    std::function<void(const std::wstring& id)> detail;       // a detail opened (the host may SetEntryCapture)
    std::function<void(const std::wstring& markdown)> copy;   // « Copier en Markdown »: the host writes the clipboard
    std::function<void(const std::wstring& id)> copyEntry;    // integration: when set, replaces copy (the host builds the full Markdown from its store)
    std::function<void(const std::wstring& id)> reopen;       // V3-B « Rouvrir » (an annotation an agent gave back)
    std::function<void()> annotate;                           // « Annoter cette page » (empty state)
    std::function<void(bool keyboard)> closed;                // the panel closed itself (close, Échap)
    std::function<void(const std::wstring&)> announce;
};
// V3 order: done > held by an agent > given back with a reason > read > waiting.
inline bool Unhandled(const HistoryEntry& e) { return e.status == EntryStatus::Pending && e.unhandled; }
inline bool ReadOnly(const HistoryEntry& e) { return e.status == EntryStatus::Pending && !e.unhandled && !e.readBy.empty(); }
inline std::wstring StatusLine(const HistoryEntry& e) {
    const std::wstring a = e.agentName.empty() ? std::wstring(AgentName(e.agent)) : e.agentName;
    if (e.status == EntryStatus::Done) return L"Terminée par " + a;
    if (e.status == EntryStatus::Working) return a;
    if (Unhandled(e)) return a + L" n'a pas pu la traiter";
    if (ReadOnly(e)) return L"Lue par " + e.readBy;
    return L"En attente";
}
inline std::wstring StatusTitle(const HistoryEntry& e) {
    const std::wstring a = e.agentName.empty() ? std::wstring(AgentName(e.agent)) : e.agentName;
    if (e.status == EntryStatus::Done) return a + L" a terminé";
    if (e.status == EntryStatus::Working) return a + L" s'en occupe";
    if (Unhandled(e)) return a + L" n'a pas pu la traiter";
    if (ReadOnly(e)) return L"Lue par " + e.readBy + (e.readAge.empty() ? L"" : L" · " + e.readAge);
    return L"En attente d'un agent";
}
inline std::wstring StepsText(const HistoryEntry& e) {
    std::wstring t; for (const auto& s : e.steps) t += (t.empty() ? L"" : L" · ") + s.label + L" " + s.time + (s.quote.empty() ? L"" : L" « " + s.quote + L" »"); return t;
}
// « Copier en Markdown » (pure).
inline std::wstring EntryMarkdown(const HistoryEntry& e) {
    std::wstring first = e.note.substr(0, e.note.find(L'\n'));
    std::wstring md = L"## " + first + L"\n\n- Site : " + e.site + L"\n- Page : " + e.page + L"\n- Statut : " + StatusTitle(e) + L"\n";
    if (!e.steps.empty()) md += L"- Suivi : " + StepsText(e) + L"\n";
    else if (!e.followUp.empty()) md += L"- Suivi : " + e.followUp + L"\n";
    md += L"\n" + e.note + L"\n";
    if (!e.summary.empty()) md += L"\n> " + e.summary + L"\n";
    return md;
}
inline bool Matches(const HistoryEntry& e, HistoryFilter f) { return f == HistoryFilter::All || (f == HistoryFilter::Done) == (e.status == EntryStatus::Done); }

namespace HistorySpec {
inline constexpr float width = 360, radius = 12, header = 50, filters = 32, filtersGap = 6, section = 30, row = 52, listPad = 4, inset = 16, bottom = 64;
inline constexpr float detailHeader = 48, capture = 150, emptyBody = 96, emptyAction = 56, filterEmpty = 72;
inline constexpr float toastH = 40, launcher = 40, gap = 8;
inline constexpr const wchar_t* empty = L"Aucune annotation pour l'instant...";
}
enum HistoryHit { HNone = 0, HClose = 1, HFilters = 2, HList = 3, HBack = 5, HCopy = 6, HDelete = 7, HAnnotate = 8, HMenuCopy = 9, HMenuDelete = 10, HReopen = 11, HFilter0 = 20, HRowBase = 1000, HMoreBase = 100000 };
struct HistoryItem { bool section = false; int entry = -1; std::wstring site; bool current = false; int count = 0; float y = 0, h = 0; };
struct HistoryLayout {
    bool detail = false, empty = false, filterEmpty = false;
    float height = 0, listTop = 0, viewport = 0, content = 0;
    D2D1_RECT_F close{}, filters{}, segment[3]{}, annotate{}, back{}, copy{}, del{}, capture{}, note{}, follow{}, page{}, help{}, reopen{};
    std::vector<HistoryItem> items;
};
struct HistoryView {
    HistoryData data; HistoryFilter filter = HistoryFilter::All; std::wstring detailId;
    int hot = -1, selected = -1, focus = 0, menuEntry = -1, menuHot = 0; bool keyboard = false, reduced = false; float scroll = 0, maxHeight = 600;
    std::map<int, Motion::InteractionVisual> iv;
    Motion::InteractionVisual V(int id) const { auto it = iv.find(id); return it == iv.end() ? Motion::InteractionVisual{} : it->second; }
    int Find(const std::wstring& id) const { for (size_t i = 0; i < data.entries.size(); ++i) if (data.entries[i].id == id) return int(i); return -1; }
    int Count(HistoryFilter f) const { int n = 0; for (auto& e : data.entries) if (Matches(e, f)) ++n; return n; }
};
inline HistoryLayout LayoutHistory(Ink& ink, const HistoryView& v) {
    using namespace HistorySpec; HistoryLayout l; const float x0 = 1, inner = width - 2;
    const int d = v.detailId.empty() ? -1 : v.Find(v.detailId);
    if (d >= 0) {
        const auto& e = v.data.entries[size_t(d)]; l.detail = true; float y = 1;
        l.back = {x0 + 10, y + 12, x0 + 36, y + 38}; l.close = {x0 + inner - 36, y + 12, x0 + inner - 10, y + 38}; l.page = {l.back.right + 6, y + 12, l.close.left - 6, y + 38};
        y += detailHeader;
        l.capture = {x0 + 12, y, x0 + inner - 12, y + capture}; y += capture + 12;
        const float nh = ink.Height(e.note, F(14, 20), inner - 32); l.note = {x0 + 16, y, x0 + inner - 16, y + nh}; y += nh + 12;
        if (!e.steps.empty()) {   // V3 « Suivi »: one line per step (icon 16, label 13/16, time at right), quotes under it
            float fh = 10 + 16 + 8;   // « Suivi » 12/16 medium, then the steps
            for (size_t i = 0; i < e.steps.size(); ++i) { fh += 16; if (!e.steps[i].quote.empty()) fh += 4 + ink.Height(L"« " + e.steps[i].quote + L" »", F(13, 18), inner - 24 - 24 - 26 - 40); if (i + 1 < e.steps.size()) fh += 10; }
            fh += 12; l.follow = {x0 + 12, y, x0 + inner - 12, y + fh}; y += fh + 12;
            if (Unhandled(e)) { const float hh = 20 + ink.Height(UnhandledHelp, F(13, 18), inner - 24 - 24); l.help = {x0 + 12, y, x0 + inner - 12, y + hh}; y += hh + 12; }
        } else {
            const std::wstring follow = e.status == EntryStatus::Done && !e.summary.empty() ? e.summary : e.followUp;
            const float fh = 10 + 18 + (follow.empty() ? 0 : 6 + ink.Height(follow, F(12, 17), inner - 24 - 24)) + 10;
            l.follow = {x0 + 12, y, x0 + inner - 12, y + fh}; y += fh + 12;
        }
        if (Unhandled(e)) {   // V3-B: Rouvrir · Copier en Markdown … Supprimer, each hugging its text
            const float rw = 20 + ink.Width(L"Rouvrir", F(12, 16, 500)), cw = 20 + ink.Width(L"Copier en Markdown", F(12, 16, 500)), dw = 20 + ink.Width(L"Supprimer", F(12, 16, 500));
            l.reopen = {x0 + 12, y, x0 + 12 + rw, y + 30}; l.copy = {l.reopen.right + 8, y, l.reopen.right + 8 + cw, y + 30}; l.del = {x0 + inner - 12 - dw, y, x0 + inner - 12, y + 30};
        } else { const float bw = (inner - 24 - 8) / 2; l.copy = {x0 + 12, y, x0 + 12 + bw, y + 30}; l.del = {l.copy.right + 8, y, x0 + inner - 12, y + 30}; }
        y += 30 + 12;
        l.height = y + 1; l.viewport = 0; return l;
    }
    float y = 1;
    l.close = {x0 + inner - 36, y + 14, x0 + inner - 10, y + 40}; y += header;
    if (v.data.entries.empty()) {
        l.empty = true; y += emptyBody; l.annotate = {x0 + 12, y + 12, x0 + inner - 12, y + 44}; y += emptyAction;
        l.height = y + 1; return l;
    }
    l.filters = {x0 + 12, y, x0 + inner - 12, y + filters};
    const float sw = (l.filters.right - l.filters.left - 6 - 8) / 3;
    for (int i = 0; i < 3; ++i) l.segment[i] = {l.filters.left + 3 + i * (sw + 4), y + 3, l.filters.left + 3 + i * (sw + 4) + sw, y + 29};
    y += filters + filtersGap;
    l.listTop = y;
    // Sections by site, the current page's site first, then by recency.
    std::vector<std::wstring> sites;
    if (!v.data.currentSite.empty()) for (auto& e : v.data.entries) if (e.site == v.data.currentSite && Matches(e, v.filter)) { sites.push_back(e.site); break; }
    for (auto& e : v.data.entries) if (Matches(e, v.filter) && std::find(sites.begin(), sites.end(), e.site) == sites.end()) sites.push_back(e.site);
    float c = 1;
    for (auto& site : sites) {
        HistoryItem s; s.section = true; s.site = site; s.current = site == v.data.currentSite; s.y = c; s.h = section;
        for (auto& e : v.data.entries) if (e.site == site && Matches(e, v.filter)) ++s.count;
        l.items.push_back(s); c += section;
        for (size_t i = 0; i < v.data.entries.size(); ++i) if (v.data.entries[i].site == site && Matches(v.data.entries[i], v.filter)) { HistoryItem r; r.entry = int(i); r.y = c; r.h = row; l.items.push_back(r); c += row; }
    }
    if (sites.empty()) { l.filterEmpty = true; c += filterEmpty; }
    c += listPad; l.content = c;
    const float room = std::max(120.f, v.maxHeight - y - 1);
    l.viewport = std::min(c, room);
    l.height = y + l.viewport + 1; return l;
}
inline float MaxScroll(const HistoryLayout& l) { return std::max(0.f, l.content - l.viewport); }
inline int HistoryHitAt(const HistoryLayout& l, const HistoryView& v, float x, float y) {
    if (Inside(l.close, x, y)) return HClose;
    if (l.detail) { if (Inside(l.back, x, y)) return HBack; if (l.reopen.right > l.reopen.left && Inside(l.reopen, x, y)) return HReopen; if (Inside(l.copy, x, y)) return HCopy; if (Inside(l.del, x, y)) return HDelete; return HNone; }
    if (l.empty) return Inside(l.annotate, x, y) ? HAnnotate : HNone;
    for (int i = 0; i < 3; ++i) if (Inside(l.segment[i], x, y)) return HFilter0 + i;
    if (y >= l.listTop && y < l.listTop + l.viewport && x > 1 && x < HistorySpec::width - 1) {
        const float cy = y - l.listTop + v.scroll;
        for (auto& it : l.items) if (!it.section && cy >= it.y && cy < it.y + it.h) {
            const float top = l.listTop + it.y - v.scroll; const D2D1_RECT_F more{HistorySpec::width - 1 - 4 - 8 - 24, top + 14, HistorySpec::width - 1 - 4 - 8, top + 38};
            return Inside(more, x, y) ? HMoreBase + it.entry : HRowBase + it.entry;
        }
    }
    return HNone;
}
inline void PaintCaptureThumb(Ink& ink, const Shot* shot, D2D1_RECT_F box, float radius, bool done, bool contain = false) {
    if (shot && shot->width) ink.Thumbnail(shot, box, radius, 1, contain);
    else {   // no image: the page colour and a bar (Paper's placeholder)
        ink.Fill(box, radius, Hex(Ink_::page)); const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2, w = (box.right - box.left) * .59f, h = std::max(7.f, (box.bottom - box.top) * .22f);
        ink.Fill({cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2}, 1, Hex(done ? 0xBDBDBDFF : 0x1C1C1CFF));
    }
    ink.Stroke(box, radius, Hex(0xFFFFFF1A), 1);
}
inline void PaintHistoryRow(Ink& ink, const HistoryView& v, const HistoryEntry& e, D2D1_RECT_F r, int index) {
    const bool hot = v.hot == index || v.selected == index || v.menuEntry == index;
    if (hot) ink.Fill(r, 8, Hex(0xFFFFFF0A));
    const bool done = e.status == EntryStatus::Done;
    // Done rows are dimmed to 70 % (Paper) on the capture and the note only: the 12 px status and
    // age keep #FFFFFF99 at full strength (6.2:1), which 70 % would bring under 4.5:1 (4.1:1).
    ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(done ? .7f : 1.f, g);   // V3: an unhandled row stays at full strength
    const float x = r.left + 8, cy = (r.top + r.bottom) / 2, more = r.right - 8 - 24;
    PaintCaptureThumb(ink, e.thumb.width ? &e.thumb : nullptr, {x, cy - 16, x + 44, cy + 16}, 5, done);
    const float tx = x + 44 + 10, tr = more - 10, top = r.top + 8;
    ink.Text(e.note.substr(0, e.note.find(L'\n')), F(13, 18, 500), {tx, top, tr, top + 18}, {1, 1, 1, 1});
    if (pushed) ink.PopOpacity(g);
    const float my = top + 20;
    const float ageW = ink.Width(e.age, F(12, 16));
    ink.Text(e.age, F(12, 16), {tr - ageW, my, tr, my + 16}, Hex(0xFFFFFF99), false, Align::Trailing);
    const D2D1_RECT_F status{tx, my, tr - ageW - 8, my + 16};
    if (e.status == EntryStatus::Working) Pieces(ink, {PLogo(e.agent, 12, 5), PText(StatusLine(e), F(12, 16), Hex(0xFFFFFF99))}, status, Align::Leading);
    else if (Unhandled(e)) Pieces(ink, {PIcon(Glyphs::warningCircle, 12, Hex(Ink_::amber), 5), PText(StatusLine(e), F(12, 16), Hex(Ink_::amber))}, status, Align::Leading);
    else if (ReadOnly(e)) Pieces(ink, {PIcon(Glyphs::eye, 12, {1, 1, 1, .7f}, 5), PText(StatusLine(e), F(12, 16), Hex(0xFFFFFF99))}, status, Align::Leading);
    else if (e.status == EntryStatus::Pending) { ink.Disc(status.left + 6, my + 8, 3, Hex(0xFFFFFF66)); ink.Text(StatusLine(e), F(12, 16), {status.left + 20, my, status.right, my + 16}, Hex(0xFFFFFF99)); }
    else Pieces(ink, {PIcon(Glyphs::check, 12, {1, 1, 1, .7f}, 5), PText(StatusLine(e), F(12, 16), Hex(0xFFFFFF99))}, status, Align::Leading);
    if (hot) {   // « … » (revealed instantly on hover / focus)
        const D2D1_RECT_F b{more, cy - 12, more + 24, cy + 12}; const auto iv = v.V(HMoreBase + index);
        ScaleScope s(ink.Target(), b, Motion::PressScale(iv.press, v.reduced));
        ink.Fill(b, 5, {1, 1, 1, (20 / 255.f) + .06f * iv.hover});
        ink.Icon(Glyphs::dots, {b.left + 5, b.top + 5, b.right - 5, b.bottom - 5}, {1, 1, 1, 1});
    }
    if (v.keyboard && v.selected == index && v.focus == HList) FocusRing(ink, Inflate(r, -1), 7, true);
}
inline D2D1_RECT_F MenuRect(const HistoryLayout& l, const HistoryView& v) {
    for (auto& it : l.items) if (!it.section && it.entry == v.menuEntry) {
        const float top = l.listTop + it.y - v.scroll, right = HistorySpec::width - 1 - 4 - 8, h = 4 + 30 + 30 + 4;
        float y = top + 40; if (y + h > l.height - 4) y = top + 12 - h;
        return {right - 196, y, right, y + h};
    }
    return {};
}
inline int MenuHitAt(const D2D1_RECT_F& m, float x, float y) {
    if (!Inside(m, x, y)) return 0; return y < m.top + 4 + 30 ? HMenuCopy : HMenuDelete;
}
inline void PaintMenu(Ink& ink, const HistoryView& v, D2D1_RECT_F m) {
    ink.Fill(m, 8, Hex(Ink_::raised));
    const D2D1_RECT_F a{m.left + 4, m.top + 4, m.right - 4, m.top + 34}, b{m.left + 4, m.top + 34, m.right - 4, m.top + 64};
    if (v.menuHot == HMenuCopy) ink.Fill(a, 6, {1, 1, 1, .08f});
    if (v.menuHot == HMenuDelete) ink.Fill(b, 6, WithAlpha(Hex(Ink_::danger), .12f));
    Pieces(ink, {PIcon(Glyphs::copy, 14, {1, 1, 1, .6f}, 8), PText(L"Copier en Markdown", F(13, 16), {1, 1, 1, .9f})}, {a.left + 10, a.top, a.right, a.bottom}, Align::Leading);
    Pieces(ink, {PIcon(Glyphs::close, 14, Hex(Ink_::danger), 8), PText(L"Supprimer", F(13, 16), Hex(Ink_::danger))}, {b.left + 10, b.top, b.right, b.bottom}, Align::Leading);
    if (v.keyboard && v.menuHot) FocusRing(ink, Inflate(v.menuHot == HMenuCopy ? a : b, -1), 5, true);
    ink.Stroke(m, 8, Hex(0xFFFFFF14), 1);
}
inline void PaintSegmented(Ink& ink, const HistoryView& v, const HistoryLayout& l) {
    ink.Fill(l.filters, 8, Hex(Ink_::raised));
    const wchar_t* names[3] = {L"Toutes", L"En cours", L"Terminées"};
    for (int i = 0; i < 3; ++i) {
        const bool on = int(v.filter) == i; const auto iv = v.V(HFilter0 + i);
        if (on) ink.Fill(l.segment[i], 6, Hex(Ink_::pressed)); else if (iv.hover > 0) ink.Fill(l.segment[i], 6, {1, 1, 1, .04f * iv.hover});
        std::vector<Piece> p{PText(names[i], F(12, 16, 500), on ? D2D1_COLOR_F{1, 1, 1, 1} : Hex(0xFFFFFFB3), 5)};
        if (i) { Font f = F(11, 14); f.tabular = true; p.push_back(PText(std::to_wstring(v.Count(HistoryFilter(i))), f, Hex(0xFFFFFF80))); }
        Pieces(ink, p, l.segment[i]);
        if (v.focus == HFilters && on) FocusRing(ink, l.segment[i], 6, v.keyboard);
    }
}
// V3 « Suivi » (Paper 5.5): « Suivi » 12/16 medium #FFFFFF99, then per step a 16 DIP icon slot (dot 6 #FFFFFF66 /
// eye 12 at 70 % / logo 12 / check 12 at 70 % / warning-circle 12 amber), label 13/16 #FFFFFFCC (amber 500 when an agent
// could not handle it), time 12/16 #FFFFFF80 at right, the quote (reason, summary, question) 13/18 #FFFFFFB3 under it.
inline void PaintSteps(Ink& ink, const HistoryEntry& e, const HistoryLayout& l) {
    using Step = HistoryEntry::Step;
    const float x = l.follow.left + 12, r = l.follow.right - 12; float y = l.follow.top + 10;
    ink.Text(L"Suivi", F(12, 16, 500), {x, y, r, y + 16}, Hex(0xFFFFFF99)); y += 16 + 8;
    for (size_t i = 0; i < e.steps.size(); ++i) {
        const auto& s = e.steps[i]; const float cy = y + 8;
        const D2D1_RECT_F icon{x + 2, cy - 6, x + 14, cy + 6};
        switch (s.kind) {
        case Step::Added: ink.Disc(x + 8, cy, 3, Hex(0xFFFFFF66)); break;
        case Step::Read: ink.Icon(Glyphs::eye, icon, {1, 1, 1, .7f}); break;
        case Step::Claimed: case Step::Released: Logo(ink, s.agent, icon); break;
        case Step::Unhandled: ink.Icon(Glyphs::warningCircle, icon, Hex(Ink_::amber)); break;
        case Step::Done: ink.Icon(Glyphs::check, icon, {1, 1, 1, .7f}); break;
        case Step::Question: case Step::Dismissed: ink.Icon(Glyphs::info, icon, {1, 1, 1, .7f}); break;
        case Step::Reopened: ink.Icon(Glyphs::ring, icon, {1, 1, 1, .5f}); break;
        }
        const float tw = ink.Width(s.time, F(12, 16));
        ink.Text(s.time, F(12, 16), {r - tw, y, r, y + 16}, Hex(0xFFFFFF80), false, Align::Trailing);
        const bool amber = s.kind == Step::Unhandled;
        ink.Text(s.label, F(13, 16, amber ? 500 : 400), {x + 26, y, r - tw - 8, y + 16}, amber ? Hex(Ink_::amber) : Hex(0xFFFFFFCC));
        y += 16;
        if (!s.quote.empty()) { const std::wstring q = L"« " + s.quote + L" »"; const float qw = r - tw - 8 - (x + 26); const float qh = ink.Height(q, F(13, 18), qw);
            ink.Text(q, F(13, 18), {x + 26, y + 4, x + 26 + qw, y + 4 + qh}, Hex(0xFFFFFFB3), true); y += 4 + qh; }
        y += 10;
    }
}
inline void PaintHistoryBody(Ink& ink, const HistoryView& v, const HistoryLayout& l) {
    using namespace HistorySpec; const float inner = width - 2;
    if (l.detail) {
        const int d = v.Find(v.detailId); if (d < 0) return; const auto& e = v.data.entries[size_t(d)];
        {   const auto iv = v.V(HBack); ScaleScope s(ink.Target(), l.back, Motion::PressScale(iv.press, v.reduced));
            if (iv.hover > 0) ink.Fill(l.back, 6, {1, 1, 1, .08f * iv.hover});
            const float cx = (l.back.left + l.back.right) / 2, cy = (l.back.top + l.back.bottom) / 2; ink.Icon(Glyphs::caretLeft, {cx - 7, cy - 7, cx + 7, cy + 7}, {1, 1, 1, .8f});
            if (v.focus == HBack) FocusRing(ink, l.back, 6, v.keyboard); }
        ink.Text(e.page, F(13, 18), l.page, Hex(0xFFFFFFB3), false, Align::Leading, true);
        CloseButton(ink, l.close, 14, v.V(HClose), v.focus == HClose, v.keyboard, v.reduced);
        PaintCaptureThumb(ink, e.capture.width ? &e.capture : e.thumb.width ? &e.thumb : nullptr, l.capture, 8, false, e.capture.width != 0);   // V3: contain
        ink.Text(e.note, F(14, 20), l.note, Hex(0xFFFFFFE6), true);
        ink.Fill(l.follow, 8, Hex(Ink_::raised));
        if (!e.steps.empty()) { PaintSteps(ink, e, l); }
        else {
        const float fx = l.follow.left + 12, fy = l.follow.top + 10, fr = l.follow.right - 12;
        const float ageW = ink.Width(e.age, F(12, 16));
        ink.Text(e.age, F(12, 16), {fr - ageW, fy, fr, fy + 18}, Hex(0xFFFFFF99), false, Align::Trailing, true);
        std::vector<Piece> head;
        if (e.status == EntryStatus::Pending) head.push_back(PIcon(Glyphs::ring, 14, {1, 1, 1, .5f}));
        else if (e.status == EntryStatus::Done) head.push_back(PIcon(Glyphs::check, 14, {1, 1, 1, .7f}));
        else head.push_back(PLogo(e.agent, 14));
        head.push_back(PText(StatusTitle(e), F(13, 18, 500), {1, 1, 1, 1}));
        Pieces(ink, head, {fx, fy, fr - ageW - 8, fy + 18}, Align::Leading);
        const std::wstring follow = e.status == EntryStatus::Done && !e.summary.empty() ? e.summary : e.followUp;
        if (!follow.empty()) ink.Text(follow, F(12, 17), {fx, fy + 24, fr, l.follow.bottom - 10}, Hex(0xFFFFFF99), true);
        }
        if (l.help.bottom > l.help.top) { ink.Fill(l.help, 8, Hex(0xFFFFFF0D)); ink.Text(UnhandledHelp, F(13, 18), {l.help.left + 12, l.help.top + 10, l.help.right - 12, l.help.bottom - 10}, Hex(0xFFFFFFCC), true); }
        if (l.reopen.right > l.reopen.left) {   // V3-B « Rouvrir »
            const auto iv = v.V(HReopen); ScaleScope s(ink.Target(), l.reopen, Motion::PressScale(iv.press, v.reduced));
            ink.Fill(l.reopen, 6, RaisedFill(iv)); ink.Text(L"Rouvrir", F(12, 16, 500), l.reopen, {1, 1, 1, 1}, false, Align::Center, true);
            if (v.focus == HReopen) FocusRing(ink, l.reopen, 6, v.keyboard); }
        {   const auto iv = v.V(HCopy); ScaleScope s(ink.Target(), l.copy, Motion::PressScale(iv.press, v.reduced));
            ink.Fill(l.copy, 6, RaisedFill(iv)); ink.Text(L"Copier en Markdown", F(12, 16, 500), l.copy, {1, 1, 1, 1}, false, Align::Center, true);
            if (v.focus == HCopy) FocusRing(ink, l.copy, 6, v.keyboard); }
        {   const auto iv = v.V(HDelete); ScaleScope s(ink.Target(), l.del, Motion::PressScale(iv.press, v.reduced));
            ink.Fill(l.del, 6, WithAlpha(Hex(Ink_::danger), .1f + .06f * iv.hover + .04f * iv.press)); ink.Text(L"Supprimer", F(12, 16, 500), l.del, Hex(Ink_::danger), false, Align::Center, true);
            if (v.focus == HDelete) FocusRing(ink, l.del, 6, v.keyboard); }
        return;
    }
    // Header.
    ink.Icon(Glyphs::history, {17, 1 + 14 + 5, 33, 1 + 14 + 21}, {1, 1, 1, .5f});
    ink.Text(L"Historique", F(16, 20, 600), {41, 1 + 14, l.close.left - 8, 1 + 40}, {1, 1, 1, 1}, false, Align::Leading, true);
    CloseButton(ink, l.close, 14, v.V(HClose), v.focus == HClose, v.keyboard, v.reduced);
    if (l.empty) {
        const float cx = width / 2, top = 1 + header + 20;
        ink.IconHalo(Glyphs::mark, {cx - 16 + 5, top + 4.6f, cx - 16 + 5 + 22, top + 4.6f + 20.73f}, {1, 1, 1, .5f}, {0, 0, 0, 0}, 0);
        ink.IconHalo(Glyphs::close, {cx - 16 + 11, top + 7.2f, cx - 16 + 31, top + 27.2f}, Hex(0x8B8B8BFF), Hex(Ink_::canvas), 22 * 256.f / 160.f);
        ink.Text(empty, F(14, 20, 500), {1, top + 36, 1 + inner, top + 56}, Hex(0xFFFFFF80), false, Align::Center);
        const auto iv = v.V(HAnnotate); ScaleScope s(ink.Target(), l.annotate, Motion::PressScale(iv.press, v.reduced));
        ink.Fill(l.annotate, 6, Accent()); ink.Fill(l.annotate, 6, {1, 1, 1, .08f * iv.hover + .06f * iv.press});
        Pieces(ink, {PText(L"Annoter cette page", F(13, 16, 500), {1, 1, 1, 1}), PKey(L"Ctrl", Hex(0x00000038), {1, 1, 1, 1}), PKey(L"Maj", Hex(0x00000038), {1, 1, 1, 1}), PKey(L"A", Hex(0x00000038), {1, 1, 1, 1})}, l.annotate);
        if (v.focus == HAnnotate) FocusRing(ink, l.annotate, 6, v.keyboard);
        return;
    }
    PaintSegmented(ink, v, l);
    const D2D1_RECT_F list{1, l.listTop, 1 + inner, l.listTop + l.viewport};
    ink.Fill({list.left, list.top, list.right, list.top + 1}, 0, Hex(0xFFFFFF0F));
    auto* t = ink.Target(); t->PushAxisAlignedClip({list.left, list.top + 1, list.right, list.bottom}, D2D1_ANTIALIAS_MODE_ALIASED);
    if (l.filterEmpty) ink.Text(v.filter == HistoryFilter::Done ? L"Aucune annotation terminée" : L"Aucune annotation en cours", F(13, 18), {list.left, list.top + 1, list.right, list.top + 1 + HistorySpec::filterEmpty}, Hex(0xFFFFFF80), false, Align::Center, true);
    for (auto& it : l.items) {
        const float top = l.listTop + it.y - v.scroll; if (top > list.bottom || top + it.h < list.top) continue;
        if (it.section) {
            const D2D1_RECT_F r{list.left + 4 + 10, top + 10, list.right - 4 - 10, top + 26};
            Font n = F(11, 14); n.tabular = true;
            std::vector<Piece> p{PText(it.site, F(12, 16, 500), Hex(0xFFFFFFB3))}; if (it.current) p.push_back(PText(L"· cette page", F(11, 14), Hex(0xFFFFFF80)));
            Pieces(ink, p, r, Align::Leading);
            ink.Text(std::to_wstring(it.count), n, r, Hex(0xFFFFFF80), false, Align::Trailing, true);
        } else PaintHistoryRow(ink, v, v.data.entries[size_t(it.entry)], {list.left + 4, top, list.right - 4, top + it.h}, it.entry);
    }
    t->PopAxisAlignedClip();
    if (MaxScroll(l) > 0) {   // a thin scroll indicator while the list overflows
        const float track = l.viewport - 8, h = std::max(24.f, track * l.viewport / l.content), y = list.top + 4 + (track - h) * (v.scroll / MaxScroll(l));
        ink.Fill({list.right - 4, y, list.right - 2, y + h}, 1, {1, 1, 1, .18f});
    }
    if (v.menuEntry >= 0) PaintMenu(ink, v, MenuRect(l, v));
}
inline void PaintHistory(Ink& ink, const HistoryView& v, const HistoryLayout& l, float height) {
    const D2D1_RECT_F all{0, 0, HistorySpec::width, height};
    ink.Fill(all, HistorySpec::radius, Hex(Ink_::canvas));
    ID2D1Layer* layer = nullptr; ID2D1Geometry* clip = nullptr; PushRoundClip(ink, all, HistorySpec::radius, layer, clip);
    PaintHistoryBody(ink, v, l);
    PopClip(ink, layer, clip);
    ink.Stroke(all, HistorySpec::radius, Hex(0xFFFFFF14), 1);
}
inline float ToastWidth(Ink& ink) { return 1 + 12 + ink.Width(L"Annotation supprimée", F(13, 16)) + 12 + 10 + ink.Width(L"Annuler", F(13, 16, 500)) + 10 + 6 + 1; }
inline D2D1_RECT_F ToastUndo(Ink& ink) { const float w = ToastWidth(ink), bw = 10 + ink.Width(L"Annuler", F(13, 16, 500)) + 10; return {w - 1 - 6 - bw, 6, w - 1 - 6, 34}; }
inline void PaintToast(Ink& ink, const Motion::InteractionVisual& iv, bool focus, bool keyboard, bool reduced) {
    const float w = ToastWidth(ink); const D2D1_RECT_F all{0, 0, w, HistorySpec::toastH};
    ink.Fill(all, 10, Hex(Ink_::canvas));
    ink.Text(L"Annotation supprimée", F(13, 16), {13, 0, w, HistorySpec::toastH}, Hex(0xFFFFFFE6), false, Align::Leading, true);
    const auto b = ToastUndo(ink);
    {   ScaleScope s(ink.Target(), b, Motion::PressScale(iv.press, reduced)); ink.Fill(b, 6, RaisedFill(iv));
        ink.Text(L"Annuler", F(13, 16, 500), b, {1, 1, 1, 1}, false, Align::Center, true); if (focus) FocusRing(ink, b, 6, keyboard); }
    ink.Stroke(all, 10, Hex(0xFFFFFF14), 1);
}

inline Timing PanelTiming() { return {}; }
class HistoryPanel : public Animator::Client {
    // UIA keys: distinct from the HistoryHit ids used as node keys (HClose = 1…), or the tree nests into itself.
    static constexpr long long PanelKey = 501, ListKey = 502, ToastKey = 503, UndoKey = 504, EmptyKey = 505, DetailTextKey = 506, ItemBase = 1000;
    static constexpr UINT_PTR ToastTimer = 31;
    HINSTANCE instance_ = nullptr; HWND parent_ = nullptr; HistoryHooks hooks_;
    Layer panel_, toast_; HistoryView view_, before_; HistoryLayout layout_, beforeLayout_;
    Presence presence_{PanelTiming()}, toastPresence_{PanelTiming()}; Crossfade fade_; Ramp height_; int stamp_ = 0, direction_ = 1;
    Motion::State motion_; RECT page_{}; bool reduced_ = false, toastHover_ = false, toastFocus_ = false, toastPressed_ = false; int hover_ = 0, pressed_ = 0;
    HistoryEntry deleted_; int deletedAt_ = -1; std::wstring deletedId_;
    bool low_ = false;   // V3: no launcher (outside the mode) => anchored 16 DIP from the bottom, not 64
    float Bottom() const { return low_ ? HistorySpec::inset : HistorySpec::bottom; }

    Ink& I() { return *Ink::Instance(); }
    float Scale() const { const UINT d = GetDpiForWindow(parent_); return (d ? d : 96) / 96.f; }
    int Px(float d) const { return int(std::lround(d * Scale())); }
    float H(Motion::Time now) const { return height_.Value(now); }
    RECT PanelRect(Motion::Time now) const {   // parent-client px, bottom-anchored, + 4 DIP of room for the scale origin
        const int r = page_.right - Px(HistorySpec::inset), b = page_.bottom - Px(Bottom()) + Px(4);
        return {r - Px(HistorySpec::width), b - Px(4) - Px(std::max(layout_.height, H(now))), r, b};
    }
    RECT ToastRect() const {
        // Left of the launcher on its line; without the launcher, left of the panel (anchored on the same line).
        const int r = page_.right - Px(HistorySpec::inset + (low_ ? HistorySpec::width : HistorySpec::launcher) + HistorySpec::gap), b = page_.bottom - Px(HistorySpec::inset);
        return {r - Px(ToastWidth(*Ink::Instance())), b - Px(HistorySpec::toastH), r, b};
    }
    void Relayout() {
        view_.maxHeight = (page_.bottom - page_.top) / Scale() - Bottom() - HistorySpec::inset;
        layout_ = LayoutHistory(I(), view_); view_.scroll = std::clamp(view_.scroll, 0.f, MaxScroll(layout_));
    }
    HistoryView Visual(const HistoryView& base, Motion::Time now) const {
        HistoryView v = base; v.reduced = reduced_;
        for (int id : {int(HClose), int(HBack), int(HReopen), int(HCopy), int(HDelete), int(HAnnotate), int(HFilter0), int(HFilter0) + 1, int(HFilter0) + 2}) v.iv[id] = motion_.Visual(id, now);
        for (size_t i = 0; i < base.data.entries.size(); ++i) v.iv[HMoreBase + int(i)] = motion_.Visual(HMoreBase + int(i), now);
        return v;
    }
    void Render(Motion::Time now) {
        if (!panel_.Window()) return;
        const RECT rect = PanelRect(now); const float winH = (rect.bottom - rect.top) / panel_.Scale();
        const float h = H(now), top = winH - 4 - h;
        const Pose p = presence_.Sample(now, reduced_);
        const HistoryView v = Visual(view_, now), b = before_; const HistoryLayout l = layout_, bl = beforeLayout_;
        const float in = fade_.Incoming(now), out = fade_.Outgoing(now); const bool fading = fade_.Moving(now) && fade_.Previous() >= 0;
        const float slide = reduced_ ? 0.f : 16.f * direction_;
        panel_.paint = [=](Ink& ink, float, float) {
            auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Translation(0, top) * base);
            Posed(ink, p, {HistorySpec::width - 20, h + 28}, [&] {
                const D2D1_RECT_F all{0, 0, HistorySpec::width, h};
                ink.Fill(all, HistorySpec::radius, Hex(Ink_::canvas));
                ID2D1Layer* layer = nullptr; ID2D1Geometry* clip = nullptr; PushRoundClip(ink, all, HistorySpec::radius, layer, clip);
                D2D1_MATRIX_3X2_F inner; ink.Target()->GetTransform(&inner);
                if (fading && out > 0) {
                    ink.Target()->SetTransform(D2D1::Matrix3x2F::Translation(-slide * (1 - out), 0) * inner);
                    ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(out, g); PaintHistoryBody(ink, b, bl); if (pushed) ink.PopOpacity(g);
                }
                ink.Target()->SetTransform(D2D1::Matrix3x2F::Translation(fading ? slide * (1 - in) : 0.f, 0) * inner);
                { ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(fading ? in : 1.f, g); PaintHistoryBody(ink, v, l); if (pushed) ink.PopOpacity(g); }
                ink.Target()->SetTransform(inner);
                PopClip(ink, layer, clip);
                ink.Stroke(all, HistorySpec::radius, Hex(0xFFFFFF14), 1);
            });
            t->SetTransform(base);
        };
        panel_.Present(rect, true);
    }
    void RenderToast(Motion::Time now) {
        if (!toast_.Window()) return;
        const Pose p = toastPresence_.Sample(now, reduced_); const auto iv = motion_.Visual(UndoKey, now); const bool focus = toastFocus_, kb = view_.keyboard, reduced = reduced_;
        const float w = ToastWidth(I());
        toast_.paint = [=](Ink& ink, float, float) { Posed(ink, p, {w, HistorySpec::toastH / 2}, [&] { PaintToast(ink, iv, focus, kb, reduced); }); };
        toast_.Present(ToastRect(), true);
    }
    void Announce(const std::wstring& text) { if (text.empty()) return; if (hooks_.announce) hooks_.announce(text); else panel_.uia.Notify(PanelKey, text.c_str(), L"AnnotationHistory"); }
    template<class F> void Change(F&& mutate, int direction = 0) {
        const auto now = Motion::Now(); const float h = H(now);
        before_ = Visual(view_, now); beforeLayout_ = layout_;
        mutate(); view_.menuEntry = -1; Relayout();
        height_.Snap(h); height_.Set(layout_.height, now, reduced_ ? 0 : Spec::panelIn, Curves::out);
        if (direction) { direction_ = direction; fade_.Go(++stamp_, now); }
        Render(now); Animator::Get().Request(this);
        if (panel_.uia.Listening()) panel_.uia.ChildrenChanged(PanelKey);
    }
    void OpenDetail(int entry, bool keyboard) {
        if (entry < 0 || entry >= int(view_.data.entries.size())) return;
        const std::wstring id = view_.data.entries[size_t(entry)].id;
        Change([&] { view_.detailId = id; view_.focus = keyboard ? HBack : 0; view_.keyboard = keyboard; }, 1);
        if (hooks_.detail) hooks_.detail(id);
        Announce(L"Annotation : " + view_.data.entries[size_t(entry)].note);
    }
    void CloseDetail(bool keyboard) {
        const std::wstring id = view_.detailId;
        Change([&] { view_.detailId.clear(); view_.selected = view_.Find(id); view_.focus = keyboard ? HList : 0; view_.keyboard = keyboard; }, -1);
        Reveal(view_.selected);
    }
    void Reveal(int entry) {
        for (auto& it : layout_.items) if (!it.section && it.entry == entry) {
            if (it.y - HistorySpec::section < view_.scroll) view_.scroll = std::max(0.f, it.y - (it.y > HistorySpec::section + 1 ? HistorySpec::section : 1));
            else if (it.y + it.h + HistorySpec::listPad > view_.scroll + layout_.viewport) view_.scroll = it.y + it.h + HistorySpec::listPad - layout_.viewport;
        }
        view_.scroll = std::clamp(view_.scroll, 0.f, MaxScroll(layout_)); Render(Motion::Now());
    }
    void Delete(int entry, bool keyboard) {
        if (entry < 0 || entry >= int(view_.data.entries.size())) return;
        FinalizeDeleted();
        deleted_ = view_.data.entries[size_t(entry)]; deletedAt_ = entry; deletedId_ = deleted_.id;
        const bool inDetail = !view_.detailId.empty();
        Change([&] {
            view_.data.entries.erase(view_.data.entries.begin() + entry); view_.detailId.clear(); view_.hot = -1;
            if (view_.selected >= int(view_.data.entries.size())) view_.selected = int(view_.data.entries.size()) - 1;
            view_.focus = keyboard ? (view_.data.entries.empty() ? HAnnotate : HList) : 0; view_.keyboard = keyboard;
        }, inDetail ? -1 : 0);
        I().ForgetBitmaps(nullptr);
        if (hooks_.remove) hooks_.remove(deletedId_);
        ShowToast();
        Announce(L"Annotation supprimée. Ctrl+Z pour annuler.");
    }
    void Undo() {
        if (deletedAt_ < 0) return;
        const auto now = Motion::Now(); KillTimer(toast_.Window(), ToastTimer); toastPresence_.Hide(now, true, reduced_); toastFocus_ = false;
        const int at = std::min(deletedAt_, int(view_.data.entries.size())); const std::wstring id = deletedId_;
        Change([&] { view_.data.entries.insert(view_.data.entries.begin() + at, deleted_); view_.selected = at; });
        deletedAt_ = -1; deletedId_.clear(); I().ForgetBitmaps(nullptr);
        if (hooks_.undo) hooks_.undo(id);
        Reveal(at); RenderToast(now); Animator::Get().Request(this);
        Announce(L"Suppression annulée");
    }
    void FinalizeDeleted() {
        if (deletedAt_ < 0) return; const std::wstring id = deletedId_; deletedAt_ = -1; deletedId_.clear();
        if (toast_.Window()) KillTimer(toast_.Window(), ToastTimer);
        if (hooks_.removeFinal) hooks_.removeFinal(id);
    }
    void ShowToast() {
        if (!toast_.Window()) return; const auto now = Motion::Now();
        toastPresence_.Show(now, true, reduced_); RenderToast(now); Animator::Get().Request(this);
        KillTimer(toast_.Window(), ToastTimer); SetTimer(toast_.Window(), ToastTimer, Spec::toastMs, nullptr);
    }
    void HideToast(bool animate = true) {
        if (!toast_.Window()) return; const auto now = Motion::Now(); KillTimer(toast_.Window(), ToastTimer);
        toastPresence_.Hide(now, animate, reduced_); toastFocus_ = false;
        if (!toastPresence_.Visible(now)) toast_.Hide(); else Animator::Get().Request(this);
    }
    void Copy(int entry) {
        if (entry < 0 || entry >= int(view_.data.entries.size())) return;
        if (hooks_.copyEntry) hooks_.copyEntry(view_.data.entries[size_t(entry)].id);
        else if (hooks_.copy) hooks_.copy(EntryMarkdown(view_.data.entries[size_t(entry)]));
        Announce(L"Copié en Markdown");
    }
    void Activate(int hit, bool keyboard) {
        view_.keyboard = keyboard;
        if (hit >= HMoreBase) { view_.menuEntry = view_.menuEntry == hit - HMoreBase ? -1 : hit - HMoreBase; view_.menuHot = keyboard ? HMenuCopy : 0; Render(Motion::Now()); return; }
        if (hit >= HRowBase) { OpenDetail(hit - HRowBase, keyboard); return; }
        if (hit >= HFilter0 && hit < HFilter0 + 3) { Change([&] { view_.filter = HistoryFilter(hit - HFilter0); view_.scroll = 0; view_.selected = -1; }); Announce(std::wstring(L"Filtre : ") + (hit == HFilter0 ? L"Toutes" : hit == HFilter0 + 1 ? L"En cours" : L"Terminées")); return; }
        switch (hit) {
        case HClose: Hide(true); if (hooks_.closed) hooks_.closed(keyboard); break;
        case HBack: CloseDetail(keyboard); break;
        case HCopy: Copy(view_.Find(view_.detailId)); break;
        case HReopen: if (hooks_.reopen && !view_.detailId.empty()) { hooks_.reopen(view_.detailId); Announce(L"Annotation rouverte"); } break;
        case HDelete: Delete(view_.Find(view_.detailId), keyboard); break;
        case HAnnotate: Hide(true); if (hooks_.annotate) hooks_.annotate(); break;
        case HMenuCopy: { const int e = view_.menuEntry; view_.menuEntry = -1; Copy(e); Render(Motion::Now()); break; }
        case HMenuDelete: { const int e = view_.menuEntry; view_.menuEntry = -1; Delete(e, keyboard); break; }
        }
    }
    void Interact() { const auto now = Motion::Now(); motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now); Render(now); Animator::Get().Request(this); }
    std::vector<int> Order() const {
        if (layout_.detail) { if (layout_.reopen.right > layout_.reopen.left) return {HBack, HClose, HReopen, HCopy, HDelete}; return {HBack, HClose, HCopy, HDelete}; }
        if (layout_.empty) return {HClose, HAnnotate};
        std::vector<int> o{HClose, HFilters}; if (!layout_.filterEmpty) o.push_back(HList); return o;
    }
    std::vector<int> Rows() const { std::vector<int> r; for (auto& it : layout_.items) if (!it.section) r.push_back(it.entry); return r; }
    void Focus(int id) {
        view_.focus = id; view_.keyboard = true; motion_.ClearInteractions();
        if (id == HList && view_.selected < 0) { auto r = Rows(); if (!r.empty()) view_.selected = r.front(); }
        if (id == HList) Reveal(view_.selected);
        Render(Motion::Now()); if (panel_.uia.Listening()) panel_.uia.Raise(id == HList ? ItemBase + view_.selected : id, UIA_AutomationFocusChangedEventId);
    }
    bool Key(WPARAM key) {
        const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
        view_.keyboard = true;
        if (ctrl && key == 'Z' && deletedAt_ >= 0) { Undo(); return true; }
        if (view_.menuEntry >= 0) {
            switch (key) {
            case VK_ESCAPE: view_.menuEntry = -1; Render(Motion::Now()); return true;
            case VK_UP: case VK_DOWN: view_.menuHot = view_.menuHot == HMenuCopy ? HMenuDelete : HMenuCopy; Render(Motion::Now()); return true;
            case VK_RETURN: case VK_SPACE: Activate(view_.menuHot ? view_.menuHot : HMenuCopy, true); return true;
            case VK_TAB: view_.menuEntry = -1; break;
            default: return true;
            }
        }
        if (key == VK_ESCAPE) { if (layout_.detail) CloseDetail(true); else { Hide(true); if (hooks_.closed) hooks_.closed(true); } return true; }
        if (key == VK_TAB) { const auto o = Order(); auto it = std::find(o.begin(), o.end(), view_.focus); int i = it == o.end() ? (shift ? int(o.size()) - 1 : 0) : int(it - o.begin()) + (shift ? -1 : 1); Focus(o[size_t((i + int(o.size())) % int(o.size()))]); return true; }
        if (view_.focus == HFilters && (key == VK_LEFT || key == VK_RIGHT)) { const int f = (int(view_.filter) + (key == VK_LEFT ? 2 : 1)) % 3; Activate(HFilter0 + f, true); view_.focus = HFilters; Render(Motion::Now()); return true; }
        if (view_.focus == HList) {
            const auto rows = Rows(); if (rows.empty()) return false;
            auto it = std::find(rows.begin(), rows.end(), view_.selected); int i = it == rows.end() ? 0 : int(it - rows.begin());
            const int page = std::max(1, int(layout_.viewport / HistorySpec::row) - 1);
            switch (key) {
            case VK_UP: i = std::max(0, i - 1); break; case VK_DOWN: i = std::min(int(rows.size()) - 1, i + 1); break;
            case VK_HOME: i = 0; break; case VK_END: i = int(rows.size()) - 1; break;
            case VK_PRIOR: i = std::max(0, i - page); break; case VK_NEXT: i = std::min(int(rows.size()) - 1, i + page); break;
            case VK_RETURN: case VK_SPACE: OpenDetail(rows[size_t(i)], true); return true;
            case VK_DELETE: Delete(rows[size_t(i)], true); return true;
            case VK_APPS: Activate(HMoreBase + rows[size_t(i)], true); return true;
            case VK_F10: if (shift) { Activate(HMoreBase + rows[size_t(i)], true); return true; } return false;
            default: return false;
            }
            view_.selected = rows[size_t(i)]; Reveal(view_.selected);
            if (panel_.uia.Listening()) panel_.uia.Raise(ItemBase + view_.selected, UIA_AutomationFocusChangedEventId);
            return true;
        }
        if ((key == VK_RETURN || key == VK_SPACE) && view_.focus && view_.focus != HFilters) { Activate(view_.focus, true); return true; }
        return false;
    }
    bool PanelHandle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const auto now = Motion::Now(); const RECT rect = PanelRect(now); const float s = 1 / panel_.Scale();
        const float top = (rect.bottom - rect.top) * s - 4 - H(now);
        const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s - top;
        auto hitAt = [&]() { if (view_.menuEntry >= 0) { const int mh = MenuHitAt(MenuRect(layout_, view_), x, y); if (mh) return mh; } return HistoryHitAt(layout_, view_, x, y); };
        switch (m) {
        case WM_NCHITTEST: { POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(panel_.Window(), &pt); r = (pt.y * s - top) >= 0 ? HTCLIENT : HTTRANSPARENT; return true; }
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, panel_.Window(), 0}; TrackMouseEvent(&t);
            const int h = hitAt(); int row = h >= HMoreBase ? h - HMoreBase : h >= HRowBase ? h - HRowBase : -1;
            if (h == HMenuCopy || h == HMenuDelete) { if (view_.menuHot != h) { view_.menuHot = h; view_.keyboard = false; } row = view_.menuEntry; }
            if (row != view_.hot) view_.hot = row;
            if (h != hover_) { hover_ = h; Interact(); } else Render(now);
            r = 0; return true; }
        case WM_MOUSELEAVE: hover_ = 0; view_.hot = -1; Interact(); r = 0; return true;
        case WM_LBUTTONDOWN: { pressed_ = hitAt(); view_.keyboard = false; if (pressed_) SetCapture(panel_.Window()); else if (view_.menuEntry >= 0) { view_.menuEntry = -1; }
            SetFocus(panel_.Window()); Interact(); r = 0; return true; }
        case WM_LBUTTONUP: { const int h = hitAt(), was = pressed_; pressed_ = 0; if (GetCapture() == panel_.Window()) ReleaseCapture(); Interact(); if (was && was == h) Activate(h, false); r = 0; return true; }
        case WM_MOUSEWHEEL: {   // internal scroll; never forwarded to the page (overscroll contained)
            if (!layout_.detail && !layout_.empty) { view_.scroll = std::clamp(view_.scroll - GET_WHEEL_DELTA_WPARAM(w) / 120.f * 3 * 20, 0.f, MaxScroll(layout_)); view_.menuEntry = -1; Render(now); }
            r = 0; return true; }
        case WM_SETFOCUS: if (!view_.focus) view_.focus = layout_.detail ? HBack : layout_.empty ? HAnnotate : HList; if (view_.focus == HList && view_.selected < 0) { auto rows = Rows(); if (!rows.empty()) view_.selected = rows.front(); } Render(now); return false;
        case WM_KILLFOCUS: view_.focus = 0; Render(now); return false;
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN: if (Key(w)) { r = 0; return true; } return false;
        case WM_APP + 3: Activate(int(w), true); r = 0; return true;
        }
        return false;
    }
    bool ToastHandle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const float s = 1 / toast_.Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        const bool in = Inside(ToastUndo(I()), x, y);
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, toast_.Window(), 0}; TrackMouseEvent(&t);
            if (!toastHover_) { toastHover_ = true; KillTimer(toast_.Window(), ToastTimer); }
            motion_.SetInteraction(in ? int(UndoKey) : 0, toastPressed_ && in ? int(UndoKey) : 0, Motion::Now()); RenderToast(Motion::Now()); Animator::Get().Request(this); r = 0; return true; }
        case WM_MOUSELEAVE: toastHover_ = false; motion_.SetInteraction(0, 0, Motion::Now()); RenderToast(Motion::Now()); Animator::Get().Request(this);
            if (toastPresence_.IsOpen() && !toastFocus_) SetTimer(toast_.Window(), ToastTimer, Spec::toastMs, nullptr); r = 0; return true;
        case WM_LBUTTONDOWN: toastPressed_ = in; if (in) SetCapture(toast_.Window()); r = 0; return true;
        case WM_LBUTTONUP: { const bool was = toastPressed_; toastPressed_ = false; if (GetCapture() == toast_.Window()) ReleaseCapture(); if (was && in) Undo(); r = 0; return true; }
        case WM_SETFOCUS: toastFocus_ = true; KillTimer(toast_.Window(), ToastTimer); RenderToast(Motion::Now()); return false;
        case WM_KILLFOCUS: toastFocus_ = false; RenderToast(Motion::Now()); if (toastPresence_.IsOpen() && !toastHover_) SetTimer(toast_.Window(), ToastTimer, Spec::toastMs, nullptr); return false;
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN: if (w == VK_RETURN || w == VK_SPACE) { Undo(); r = 0; return true; } if (w == VK_ESCAPE) { FinalizeDeleted(); HideToast(); r = 0; return true; } return false;
        case WM_TIMER: if (w == ToastTimer) { KillTimer(toast_.Window(), ToastTimer); FinalizeDeleted(); HideToast(); r = 0; return true; } return false;
        case WM_APP + 4: Undo(); r = 0; return true;
        }
        return false;
    }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> n; if (!presence_.IsOpen()) return n;
        const auto now = Motion::Now(); const RECT rect = PanelRect(now); const float top = (rect.bottom - rect.top) / panel_.Scale() - 4 - H(now);
        auto at = [&](D2D1_RECT_F r) { r.top += top; r.bottom += top; return panel_.Screen(r); };
        Accessibility::Node p; p.key = PanelKey; p.role = Accessibility::Role::Group; p.name = L"Historique"; p.automationId = L"AnnotationHistory"; p.focusable = false; GetWindowRect(panel_.Window(), &p.bounds); n.push_back(p);
        auto add = [&](long long key, Accessibility::Role role, const std::wstring& name, D2D1_RECT_F r, long long parent = PanelKey) -> Accessibility::Node& {
            Accessibility::Node x; x.key = key; x.parent = parent; x.role = role; x.name = name; x.bounds = at(r); n.push_back(x); return n.back(); };
        if (layout_.detail) {
            const int d = view_.Find(view_.detailId); if (d < 0) return n; const auto& e = view_.data.entries[size_t(d)];
            add(HBack, Accessibility::Role::Button, L"Retour à l'historique", layout_.back).accelerator = L"Échap";
            add(HClose, Accessibility::Role::Button, L"Fermer l'historique", layout_.close);
            auto& t = add(DetailTextKey, Accessibility::Role::Text, e.page, layout_.note); t.value = e.note; t.status = StatusTitle(e) + L" · " + e.age; t.help = e.steps.empty() ? e.followUp : StepsText(e) + (Unhandled(e) ? std::wstring(L". ") + UnhandledHelp : L""); t.focusable = false;
            if (layout_.reopen.right > layout_.reopen.left) add(HReopen, Accessibility::Role::Button, L"Rouvrir", layout_.reopen).help = L"Remettre l'annotation en attente pour un agent";
            add(HCopy, Accessibility::Role::Button, L"Copier en Markdown", layout_.copy);
            add(HDelete, Accessibility::Role::Button, L"Supprimer", layout_.del).help = L"Annulable pendant 6 secondes";
            return n;
        }
        add(HClose, Accessibility::Role::Button, L"Fermer l'historique", layout_.close).accelerator = L"Échap";
        if (layout_.empty) {
            auto& t = add(EmptyKey, Accessibility::Role::Text, HistorySpec::empty, {1, 1 + HistorySpec::header, HistorySpec::width - 1, 1 + HistorySpec::header + HistorySpec::emptyBody}); t.focusable = false;
            add(HAnnotate, Accessibility::Role::Button, L"Annoter cette page", layout_.annotate).accelerator = L"Ctrl+Maj+A";
            return n;
        }
        auto& tabs = add(HFilters, Accessibility::Role::Tab, L"Filtres", layout_.filters); tabs.focusable = false;
        const wchar_t* names[3] = {L"Toutes", L"En cours", L"Terminées"};
        for (int i = 0; i < 3; ++i) { auto& s = add(HFilter0 + i, Accessibility::Role::TabItem, std::wstring(names[i]) + (i ? L" (" + std::to_wstring(view_.Count(HistoryFilter(i))) + L")" : L""), layout_.segment[i], HFilters); s.selected = int(view_.filter) == i; s.position = i + 1; s.count = 3; }
        auto& list = add(ListKey, Accessibility::Role::List, L"Annotations", {1, layout_.listTop, HistorySpec::width - 1, layout_.listTop + layout_.viewport}); list.focusable = false;
        const auto rows = Rows(); int pos = 0;
        for (auto& it : layout_.items) {
            if (it.section) continue; const auto& e = view_.data.entries[size_t(it.entry)];
            const float y = layout_.listTop + it.y - view_.scroll;
            auto& r = add(ItemBase + it.entry, Accessibility::Role::ListItem, e.note, {5, y, HistorySpec::width - 5, y + it.h}, ListKey);
            r.status = StatusLine(e) + L" · " + e.age; r.help = e.site; r.selected = view_.selected == it.entry; r.position = ++pos; r.count = int(rows.size());
            r.offscreen = y + it.h < layout_.listTop || y > layout_.listTop + layout_.viewport; r.accelerator = L"Entrée : détail, Suppr : supprimer";
        }
        return n;
    }
    std::vector<Accessibility::Node> ToastNodes() {
        std::vector<Accessibility::Node> n; if (!toastPresence_.IsOpen()) return n;
        Accessibility::Node g; g.key = ToastKey; g.role = Accessibility::Role::Group; g.name = L"Annotation supprimée"; g.focusable = false; GetWindowRect(toast_.Window(), &g.bounds); n.push_back(g);
        Accessibility::Node u; u.key = UndoKey; u.parent = ToastKey; u.name = L"Annuler"; u.accelerator = L"Ctrl+Z"; u.bounds = toast_.Screen(ToastUndo(I())); u.automationId = L"AnnotationUndo"; n.push_back(u);
        return n;
    }
    bool Ensure() {
        if (panel_.Window()) return true;
        Layer::Options o; o.child = true; o.tabStop = true; o.title = L"Historique";
        Layer::Options t; t.child = true; t.tabStop = true; t.title = L"Annotation supprimée";
        if (!panel_.Create(instance_, parent_, o) || !toast_.Create(instance_, parent_, t)) return false;
        panel_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return PanelHandle(m, w, l, r); };
        toast_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return ToastHandle(m, w, l, r); };
        Accessibility::Host h; h.window = panel_.Window(); h.nodes = [this] { return Nodes(); };
        h.focused = [this]() -> long long { return GetFocus() != panel_.Window() ? 0 : view_.focus == HList ? ItemBase + view_.selected : view_.focus == HFilters ? HFilter0 + int(view_.filter) : view_.focus; };
        h.invoke = [this](long long key) { PostMessageW(panel_.Window(), WM_APP + 3, WPARAM(key >= ItemBase && key < HMoreBase ? HRowBase + (key - ItemBase) : key), 0); };
        h.select = h.invoke;
        panel_.uia.Attach(std::move(h));
        Accessibility::Host th; th.window = toast_.Window(); th.nodes = [this] { return ToastNodes(); };
        th.focused = [this]() -> long long { return toastFocus_ ? UndoKey : 0; };
        th.invoke = [this](long long) { PostMessageW(toast_.Window(), WM_APP + 4, 0, 0); };
        toast_.uia.Attach(std::move(th));
        motion_.EnablePolish();
        return true;
    }
public:
    std::function<void()> idle;   // nothing left on screen (set by the launcher)
    ~HistoryPanel() { if (Animator::Exists()) Animator::Get().Forget(this); }
    void Init(HINSTANCE instance, HWND parent, HistoryHooks hooks) { instance_ = instance; parent_ = parent; hooks_ = std::move(hooks); }
    bool Created() const { return panel_.Window() != nullptr; }
    bool Visible() const { return presence_.IsOpen(); }
    bool ToastVisible() const { return toastPresence_.IsOpen(); }
    HWND Window() const { return panel_.Window(); }
    HWND ToastWindow() const { return toast_.Window(); }
    void Place(RECT page) {
        page_ = page; const auto now = Motion::Now();
        if (!panel_.Window()) return;
        if (presence_.Visible(now)) { Relayout(); height_.Snap(layout_.height); Render(now); }
        if (toastPresence_.Visible(now)) RenderToast(now);
    }
    bool Show(const HistoryData& data, bool keyboard, bool reduced) {
        if (!Ink::Instance() || !Ensure()) return false;
        const auto now = Motion::Now(); reduced_ = reduced; motion_.SetReducedMotion(reduced);
        const bool was = presence_.IsOpen();
        if (!was) { view_ = HistoryView{}; fade_.Reset(0); }
        view_.data = data; view_.keyboard = keyboard; view_.hot = -1; view_.menuEntry = -1;
        if (!view_.detailId.empty() && view_.Find(view_.detailId) < 0) view_.detailId.clear();
        I().ForgetBitmaps(nullptr); Relayout(); before_ = view_; beforeLayout_ = layout_;
        if (!was) height_.Snap(layout_.height); else height_.Set(layout_.height, now, reduced ? 0 : Spec::panelIn, Curves::out);
        presence_.Show(now, true, reduced); Render(now); Animator::Get().Request(this);
        if (keyboard) { view_.focus = layout_.empty ? HAnnotate : HList; SetFocus(panel_.Window()); }
        if (panel_.uia.Listening()) { panel_.uia.ChildrenChanged(PanelKey); if (!was) panel_.uia.Raise(PanelKey, UIA_Window_WindowOpenedEventId); }
        if (!was) Announce(data.entries.empty() ? std::wstring(L"Historique. ") + HistorySpec::empty : L"Historique, " + Plural(int(data.entries.size()), L"annotation", L"annotations"));
        return true;
    }
    // New data while open (the host watches its store only while the panel is visible).
    void Update(const HistoryData& data) { if (!presence_.IsOpen()) return; Change([&] { view_.data = data; if (!view_.detailId.empty() && view_.Find(view_.detailId) < 0) view_.detailId.clear(); }); I().ForgetBitmaps(nullptr); }
    // Integration (card « Voir l'annotation »): open straight on one entry's detail.
    void ShowDetail(const std::wstring& id) { const int i = view_.Find(id); if (i >= 0 && view_.detailId != id) OpenDetail(i, false); }
    const std::wstring& DetailId() const { return view_.detailId; }
    void SetEntryCapture(const std::wstring& id, const Shot& capture) { const int i = view_.Find(id); if (i < 0) return; view_.data.entries[size_t(i)].capture = capture; I().ForgetBitmaps(nullptr); Render(Motion::Now()); }
    void Hide(bool animate = true) {
        if (!panel_.Window() || !presence_.IsOpen()) return; const auto now = Motion::Now();
        presence_.Hide(now, animate, reduced_); hover_ = pressed_ = 0; view_.menuEntry = -1;
        if (GetFocus() == panel_.Window()) SetFocus(parent_);
        if (!presence_.Visible(now)) panel_.Hide(); else { Render(now); Animator::Get().Request(this); }
        if (panel_.uia.Listening()) panel_.uia.Raise(PanelKey, UIA_Window_WindowClosedEventId);
    }
    void Focus() { if (panel_.Window() && presence_.IsOpen()) { view_.keyboard = true; SetFocus(panel_.Window()); } }
    // V3: anchored above the launcher (mode on) or 16 DIP from the bottom (no launcher). Re-placed at once.
    void SetLowAnchor(bool low) { if (low == low_) return; low_ = low; Place(page_); }
    bool LowAnchor() const { return low_; }
    // Nothing on screen, nothing pending (a deletion still undoable keeps the toast's window).
    bool Idle(Motion::Time now) const { return !presence_.Visible(now) && !toastPresence_.Visible(now) && deletedAt_ < 0; }
    // V3 « au repos, zéro fenêtre »: the windows are destroyed once idle; Ensure() recreates them at the next Show.
    bool ReleaseWindows() {
        if (!panel_.Window() && !toast_.Window()) return true;
        if (!Idle(Motion::Now())) return false;
        if (Animator::Exists()) Animator::Get().Forget(this);
        if (toast_.Window()) KillTimer(toast_.Window(), ToastTimer);
        panel_.Destroy(); toast_.Destroy(); hover_ = pressed_ = 0; toastHover_ = toastFocus_ = toastPressed_ = false; motion_.ClearInteractions(); return true;
    }
    bool Frame(Motion::Time now) override {
        bool more = false;
        if (presence_.Visible(now)) { Render(now); more |= presence_.Moving(now) || fade_.Moving(now) || height_.Moving(now); } else panel_.Hide();
        if (toastPresence_.Visible(now)) { RenderToast(now); more |= toastPresence_.Moving(now); } else toast_.Hide();
        more |= motion_.NextWake(now).delay.has_value();
        if (!more && idle && Idle(now)) idle();   // the launcher may destroy the windows now (outside the mode)
        return more;
    }
    // ---- tests / QA
    HistoryView& ViewForTest() { return view_; }
    const HistoryLayout& LayoutForTest() const { return layout_; }
    Layer& LayerForTest() { return panel_; }
    Layer& ToastForTest() { return toast_; }
    Presence& PresenceForTest() { return presence_; }
    bool KeyForTest(WPARAM key) { return Key(key); }
    void ActivateForTest(int hit) { Activate(hit, true); }
    void ExpireToastForTest() { LRESULT r; ToastHandle(WM_TIMER, ToastTimer, 0, r); }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
    std::vector<Accessibility::Node> ToastNodesForTest() { return ToastNodes(); }
    float ScrollForTest() const { return view_.scroll; }
    bool WheelForTest(int delta) { LRESULT r; return PanelHandle(WM_MOUSEWHEEL, MAKEWPARAM(0, delta), 0, r); }
};
}} // namespace NativeChrome::Annotations
