#pragma once
// Note popover (Paper « Popover — note (revu) » WKR-0; project list WIO-0; tooltip V3 W6W-0).
//   340 DIP, raised #292929, border 1 #FFFFFF0F, radius 10.
//   A  padding 12/12/8, gap 6: « Remarque pour l'agent » 12/16 #FFFFFF99; native multi-line
//      EDIT in a field min 84 (padding 10/12, #FFFFFF0A, radius 8, focused border #3E67FF99
//      + 8 DIP accent glow at 50 %), note 14/20 #FFFFFFE6.
//   B  padding 0/12/12: project selector h 32, radius 6, #FFFFFF0A (open: #FFFFFF14 + inset
//      ring accent 60 %), padding 10/8, gap 8: « Projet » 12/16 #FFFFFF99 (w 44), chip 8x8
//      radius 2 #FFFFFF80, name 14/18 #FFFFFFE6, caret 16 at 70 %.
//   C  « Ce que l'agent verra »: #FFFFFF05, top border #FFFFFF0F, padding 12/14/14, gap 10:
//      header 14/18 medium + info 16 (60 %) + caret 16 (70 %, up when open); rows gap 6:
//      label w 56 12/18 #FFFFFF99, gap 10, value JetBrains Mono 12/18 #FFFFFFCC (Texte:
//      Inter 13/18); capture row (padding-top 2, gap 12): thumbnail 96x56 radius 8 border
//      #FFFFFF1F, checkbox 16 radius 3 accent + check 12, « Inclure la capture » 13/18,
//      « Recadrée · 960×420 » 12/16 #FFFFFF99; provider mention 12/16 #FFFFFF99.
//   D  top border #FFFFFF0F, padding 10/12/12, gap 8: « Annuler » (#333333) + key cap
//      « Échap » (#FFFFFF14, #FFFFFFCC) and « Ajouter » (accent) + « Entrée » (#00000038):
//      h 32, radius 6, 14/18 medium -0.01em, gap 6, centred, equal widths.
//   List (WIO-0): 316 wide, raised, border #FFFFFF14, radius 8, padding 4; rows radius 4,
//      padding 7/10/8/8, gap 10: name 14/20 medium, origin 13/18 #FFFFFF99, check 16 on the
//      chosen row; hot row #FFFFFF14; separator 1 #FFFFFF14 (margins 4); « Nouveau projet
//      pour cette origine… » (plus 16 at 70 %, 14/20 #FFFFFFE6, padding 7).
//   Tooltip (V3): 236 wide at (72, 36) of section C, #171717, border #FFFFFF14, radius 6,
//      padding 8/10, 13/18 #FFFFFFE6.
// Paper's drop shadows on the list/tooltip are NOT drawn: the repository's popup rule is
// « zero drop shadow » (src/native-ui.h, user decision 2026-11-09).
// Motion: the popover is born from the target (scale 0.96 -> 1 about the point of its edge
// nearest the target, 8 DIP lift toward the target, opacity), through a click-through
// ghost layer; the real window (EDIT focused, keystrokes accepted from the first frame)
// stays at alpha 0 until the spring settles. The exit replays the path in 150 ms.
// Reduced motion: the real window only fades (alpha), no ghost, no scale, no lift.
#include "annotation-kit.h"
#include "native-ui.h"
#include <commctrl.h>

namespace NativeChrome { namespace Annotations {
struct Project { std::wstring id, label, origin; };
struct NoteContext { std::wstring page, element, text, styles, shotLabel; };
enum class CaptureState { Pending, Ready, Unavailable };
enum class NoteStatus { Editing, Saving, Failed };
enum NoteHit { NoteNone = 0, NoteField = 1, NoteProject = 2, NoteToggle = 3, NoteInfo = 4, NoteInclude = 5, NoteCancel = 6, NoteAdd = 7 };

namespace NoteSpec {
inline constexpr float width = 340, border = 1, radius = 10;
inline constexpr float aPadTop = 12, aPadX = 12, aPadBottom = 8, aGap = 6, labelLine = 16;
inline constexpr float fieldMin = 84, fieldMax = 164, fieldPadY = 10, fieldPadX = 12, fieldRadius = 8, noteLine = 20;
inline constexpr float bPadBottom = 12, selector = 32, selectorRadius = 6, selPadL = 10, selPadR = 8, selGap = 8, selLabel = 44, chip = 8;
inline constexpr float cPadTop = 12, cPadX = 14, cPadBottom = 14, cGap = 10, headerLine = 18, rowGap = 6, rowLabel = 56, rowGapX = 10, rowLine = 18;
inline constexpr float thumbW = 96, thumbH = 56, captureGap = 12, capturePadTop = 2;
inline constexpr float dPadTop = 10, dPadX = 12, dPadBottom = 12, dGap = 8, button = 32;
inline constexpr float listW = 316, listX = 13, listPad = 4, listRadius = 8, listRow = 54, listNew = 34, listSep = 9;
inline constexpr float tipW = 236, tipX = 72, tipY = 36;
inline constexpr const wchar_t* tipText = L"Ces informations accompagnent votre note. L'agent s'en sert, avec la capture, pour retrouver l'élément dans le code.";
inline constexpr const wchar_t* mention = L"Lisible par vos agents connectés. Leur fournisseur de modèle peut aussi la recevoir.";
inline constexpr const wchar_t* placeholder = L"Ex. : aligner ce bouton à droite sur mobile";
}
struct NoteView {
    std::wstring note; bool drawNote = true, fieldFocused = true, fieldError = false; float fieldHeight = NoteSpec::fieldMin;
    std::wstring projectLabel; bool listOpen = false;
    NoteContext context; bool expanded = true, tooltip = false; CaptureState capture = CaptureState::Ready; bool includeShot = true; const Shot* shot = nullptr;
    NoteStatus status = NoteStatus::Editing; std::wstring message;
    int focus = 0; bool keyboard = false, reduced = false; float spinner = 0;
    std::map<int, Motion::InteractionVisual> visuals;
    Motion::InteractionVisual V(int id) const { auto it = visuals.find(id); return it == visuals.end() ? Motion::InteractionVisual{} : it->second; }
};
struct NoteLayout {
    float height = 0, lineC = 0, lineD = 0;
    D2D1_RECT_F label{}, field{}, selector{}, header{}, info{}, caret{}, thumb{}, include{}, includeLabel{}, sub{}, mention{}, message{}, cancel{}, add{};
    struct Row { const wchar_t* label; D2D1_RECT_F labelBox, value; bool mono; int index; } rows[4]{};
    int rowCount = 0;
};
inline const std::wstring& RowValue(const NoteContext& c, int i) { return i == 0 ? c.page : i == 1 ? c.element : i == 2 ? c.text : c.styles; }
inline std::wstring AddLabel(NoteStatus s) { return s == NoteStatus::Saving ? L"Ajout…" : s == NoteStatus::Failed ? L"Réessayer" : L"Ajouter"; }
inline NoteLayout LayoutNote(Ink& ink, const NoteView& v) {
    using namespace NoteSpec; NoteLayout l; const float x0 = border, inner = width - 2 * border;
    float y = border + aPadTop;
    l.label = {x0 + aPadX, y, x0 + inner - aPadX, y + labelLine}; y += labelLine + aGap;
    l.field = {x0 + aPadX, y, x0 + inner - aPadX, y + v.fieldHeight}; y += v.fieldHeight + aPadBottom;
    l.selector = {x0 + aPadX, y, x0 + inner - aPadX, y + selector}; y += selector + bPadBottom;
    l.lineC = y; y += 1 + cPadTop;
    l.header = {x0 + cPadX, y, x0 + inner - cPadX, y + headerLine};
    const float hw = ink.Width(L"Ce que l'agent verra", F(14, 18, 500));
    l.info = {l.header.left + hw + 6, y + 1, l.header.left + hw + 22, y + 17};
    l.caret = {l.header.right - 16, y + 1, l.header.right, y + 17};
    y += headerLine;
    if (v.expanded) {
        y += cGap;
        static const wchar_t* labels[4] = {L"Page", L"Élément", L"Texte", L"Styles"};
        const float valueW = inner - 2 * cPadX - rowLabel - rowGapX;
        for (int i = 0; i < 4; ++i) {
            const std::wstring& value = RowValue(v.context, i); if (value.empty()) continue;
            const bool mono = i != 2; const Font f = mono ? Mono(12, 18) : F(13, 18);
            const float h = ink.Height(value, f, valueW);
            if (l.rowCount) y += rowGap;
            auto& r = l.rows[l.rowCount++]; r.label = labels[i]; r.mono = mono; r.index = i;
            r.labelBox = {x0 + cPadX, y, x0 + cPadX + rowLabel, y + rowLine};
            r.value = {r.labelBox.right + rowGapX, y, r.labelBox.right + rowGapX + valueW, y + h};
            y += h;
        }
        y += cGap + capturePadTop;
        l.thumb = {x0 + cPadX, y, x0 + cPadX + thumbW, y + thumbH};
        const float cx = l.thumb.right + captureGap, top = y + (thumbH - 40) / 2;
        l.include = {cx, top + 1, cx + 16, top + 17};
        l.includeLabel = {cx + 24, top, x0 + inner - cPadX, top + 18};
        l.sub = {cx, top + 24, x0 + inner - cPadX, top + 40};
        y += thumbH + cGap;
        const float mw = inner - 2 * cPadX, mh = ink.Height(mention, F(12, 16), mw);
        l.mention = {x0 + cPadX, y, x0 + cPadX + mw, y + mh}; y += mh;
    }
    y += cPadBottom;
    l.lineD = y; y += 1 + dPadTop;
    if (!v.message.empty()) {
        const float mw = inner - 2 * dPadX - 22, mh = ink.Height(v.message, F(12, 16), mw);
        l.message = {x0 + dPadX, y, x0 + inner - dPadX, y + mh}; y += mh + dGap;
    }
    const float bw = (inner - 2 * dPadX - dGap) / 2;
    l.cancel = {x0 + dPadX, y, x0 + dPadX + bw, y + button};
    l.add = {l.cancel.right + dGap, y, x0 + inner - dPadX, y + button};
    y += button + dPadBottom + border;
    l.height = y; return l;
}
inline int NoteHitAt(const NoteLayout& l, const NoteView& v, float x, float y) {
    if (Inside(l.field, x, y)) return NoteField;
    if (Inside(l.selector, x, y)) return NoteProject;
    if (Inside(Inflate(l.info, 4), x, y)) return NoteInfo;
    if (Inside({l.header.left - 4, l.header.top - 4, l.header.right + 4, l.header.bottom + 4}, x, y)) return NoteToggle;
    if (v.expanded && (Inside(Inflate(l.include, 4), x, y) || Inside(l.includeLabel, x, y))) return NoteInclude;
    if (Inside(l.cancel, x, y)) return NoteCancel;
    if (Inside(l.add, x, y)) return NoteAdd;
    return NoteNone;
}
inline bool IncludeEnabled(const NoteView& v) { return v.capture != CaptureState::Unavailable; }

// A button with a centred label + key cap (Paper WQA-0 / WQE-0).
inline void KeyButton(Ink& ink, D2D1_RECT_F r, const std::wstring& label, const std::wstring& key, D2D1_COLOR_F fill, D2D1_COLOR_F cap, D2D1_COLOR_F capText,
                      const Motion::InteractionVisual& iv, bool enabled, bool focus, bool keyboard, bool reduced, float labelOpacity = 1) {
    ScaleScope scale(ink.Target(), r, Motion::PressScale(iv.press, reduced, enabled));
    ink.Fill(r, 6, fill);
    if (enabled && (iv.hover > 0 || iv.press > 0)) ink.Fill(r, 6, {1, 1, 1, .06f * iv.hover + .06f * iv.press});
    const Font f = F(14, 18, 500, -.01f); const float tw = ink.Width(label, f), kw = key.empty() ? 0 : KeycapWidth(ink, key) + 6;
    const float x = (r.left + r.right - tw - kw) / 2, cy = (r.top + r.bottom) / 2;
    ink.Text(label, f, {x, r.top, x + tw + 2, r.bottom}, {1, 1, 1, labelOpacity}, false, Align::Leading, true);
    if (!key.empty()) Keycap(ink, key, x + tw + 6, cy, cap, capText);
    if (focus) FocusRing(ink, r, 6, keyboard);
}
inline void Checkbox(Ink& ink, D2D1_RECT_F box, bool checked, bool enabled) {
    const float a = enabled ? 1 : Design::Text::disabled;
    if (checked) { ink.Fill(box, 3, Accent(a)); ink.Icon(Glyphs::check, {box.left + 2, box.top + 2, box.right - 2, box.bottom - 2}, {1, 1, 1, a}); }
    else ink.Stroke(box, 3, {1, 1, 1, .4f * a}, 1.5f);
}
inline void Spinner(Ink& ink, D2D1_RECT_F box, float degrees, D2D1_COLOR_F c) { ink.Icon(Glyphs::ring, box, c, degrees); }

inline void PaintNote(Ink& ink, const NoteView& v, const NoteLayout& l) {
    using namespace NoteSpec;
    const D2D1_RECT_F all{0, 0, width, l.height};
    ink.Fill(all, radius, Hex(Ink_::raised));
    // A — note.
    ink.Text(L"Remarque pour l'agent", F(12, 16), l.label, Hex(0xFFFFFF99));
    if (v.fieldFocused && !v.fieldError) ink.Glow(l.field, fieldRadius, Accent(.5f));
    ink.Fill(l.field, fieldRadius, Hex(0xFFFFFF0A));
    ink.Stroke(l.field, fieldRadius, v.fieldError ? WithAlpha(Hex(Ink_::danger), .6f) : v.fieldFocused ? Accent(.6f) : Hex(0xFFFFFF14), 1);
    if (v.drawNote) {
        const D2D1_RECT_F text{l.field.left + fieldPadX, l.field.top + fieldPadY, l.field.right - fieldPadX, l.field.bottom - fieldPadY};
        auto* t = ink.Target(); t->PushAxisAlignedClip(text, D2D1_ANTIALIAS_MODE_ALIASED);
        if (v.note.empty()) ink.Text(placeholder, F(14, 20), text, Hex(0xFFFFFF99), true);
        else ink.Text(v.note, F(14, 20), text, Hex(0xFFFFFFE6), true);
        t->PopAxisAlignedClip();
    }
    // B — project.
    {
        const auto iv = v.V(NoteProject); const auto& r = l.selector;
        ink.Fill(r, selectorRadius, Hex(v.listOpen ? 0xFFFFFF14 : 0xFFFFFF0A));
        if (!v.listOpen && iv.hover > 0) ink.Fill(r, selectorRadius, {1, 1, 1, (10 / 255.f) * iv.hover});
        if (v.listOpen) ink.Stroke(r, selectorRadius, Accent(.6f), 1);
        const float cy = (r.top + r.bottom) / 2;
        ink.Text(L"Projet", F(12, 16), {r.left + selPadL, r.top, r.left + selPadL + selLabel, r.bottom}, Hex(0xFFFFFF99), false, Align::Leading, true);
        const float chipX = r.left + selPadL + selLabel + selGap;
        ink.Fill({chipX, cy - chip / 2, chipX + chip, cy + chip / 2}, 2, Hex(0xFFFFFF80));
        const float nameX = chipX + chip + selGap;
        ink.Text(v.projectLabel.empty() ? std::wstring(L"Aucun projet") : v.projectLabel, F(14, 18), {nameX, r.top, r.right - selPadR - 16 - selGap, r.bottom}, Hex(v.projectLabel.empty() ? 0xFFFFFF99 : 0xFFFFFFE6), false, Align::Leading, true);
        ink.Icon(Glyphs::caretDown, {r.right - selPadR - 16, cy - 8, r.right - selPadR, cy + 8}, {1, 1, 1, .7f}, v.listOpen ? 180.f : 0.f);
        if (v.focus == NoteProject) FocusRing(ink, r, selectorRadius, v.keyboard);
    }
    // C — what the agent will see.
    ink.Fill({border, l.lineC, width - border, l.lineD}, 0, Hex(0xFFFFFF05));
    ink.Fill({border, l.lineC, width - border, l.lineC + 1}, 0, Hex(0xFFFFFF0F));
    ink.Text(L"Ce que l'agent verra", F(14, 18, 500), {l.header.left, l.header.top, l.info.left, l.header.bottom}, Hex(0xFFFFFFE6));
    ink.Icon(Glyphs::info, l.info, {1, 1, 1, .6f + .3f * v.V(NoteInfo).hover});
    ink.Icon(Glyphs::caretDown, l.caret, {1, 1, 1, .7f + .2f * v.V(NoteToggle).hover}, v.expanded ? 180.f : 0.f);
    if (v.focus == NoteToggle) FocusRing(ink, {l.header.left - 2, l.header.top - 2, l.header.right + 2, l.header.bottom + 2}, 4, v.keyboard);
    if (v.focus == NoteInfo) FocusRing(ink, l.info, 8, v.keyboard);
    if (v.expanded) {
        for (int i = 0; i < l.rowCount; ++i) {
            const auto& r = l.rows[i];
            ink.Text(r.label, F(12, 18), r.labelBox, Hex(0xFFFFFF99));
            const std::wstring& value = RowValue(v.context, r.index);
            ink.Text(value, r.mono ? Mono(12, 18) : F(13, 18), r.value, Hex(0xFFFFFFCC), true);
        }
        // Thumbnail / capture states (static cue for each: image, ring + text, text).
        const bool ready = v.capture == CaptureState::Ready;
        if (ready && v.shot && v.shot->width) ink.Thumbnail(v.shot, l.thumb, 8, v.includeShot ? 1.f : .45f);
        else if (ready) ink.Fill(l.thumb, 8, Hex(0xF4F4F3FF));
        else ink.Fill(l.thumb, 8, Hex(0xFFFFFF0A));
        if (v.capture == CaptureState::Pending) { const float cx = (l.thumb.left + l.thumb.right) / 2, cy = (l.thumb.top + l.thumb.bottom) / 2; Spinner(ink, {cx - 8, cy - 8, cx + 8, cy + 8}, v.spinner, Accent()); }
        if (v.capture == CaptureState::Unavailable) { const float cx = (l.thumb.left + l.thumb.right) / 2, cy = (l.thumb.top + l.thumb.bottom) / 2; ink.Icon(Glyphs::danger, {cx - 8, cy - 8, cx + 8, cy + 8}, {1, 1, 1, .5f}); }
        ink.Stroke(l.thumb, 8, Hex(0xFFFFFF1F), 1);
        const bool enabled = IncludeEnabled(v);
        {
            ScaleScope scale(ink.Target(), l.include, Motion::PressScale(v.V(NoteInclude).press, v.reduced, enabled));
            Checkbox(ink, l.include, v.includeShot && enabled, enabled);
        }
        ink.Text(L"Inclure la capture", F(13, 18), l.includeLabel, enabled ? Hex(0xFFFFFFE6) : D2D1_COLOR_F{1, 1, 1, Design::Text::disabled});
        if (v.focus == NoteInclude) FocusRing(ink, l.include, 3, v.keyboard);
        const std::wstring sub = v.capture == CaptureState::Pending ? L"Capture en cours…" : v.capture == CaptureState::Unavailable ? L"Capture indisponible · envoi sans image" : v.context.shotLabel;
        ink.Text(sub, F(12, 16), l.sub, Hex(0xFFFFFF99));
        ink.Text(mention, F(12, 16), l.mention, Hex(0xFFFFFF99), true);
    }
    // D — actions.
    ink.Fill({border, l.lineD, width - border, l.lineD + 1}, 0, Hex(0xFFFFFF0F));
    if (!v.message.empty()) {
        ink.Icon(Glyphs::danger, {l.message.left, l.message.top + 1, l.message.left + 14, l.message.top + 15}, Hex(Ink_::danger));
        ink.Text(v.message, F(12, 16), {l.message.left + 22, l.message.top, l.message.right, l.message.bottom}, Hex(Ink_::danger), true);
    }
    const bool busy = v.status == NoteStatus::Saving;
    KeyButton(ink, l.cancel, L"Annuler", L"Échap", Hex(Ink_::hover), Hex(0xFFFFFF14), Hex(0xFFFFFFCC), v.V(NoteCancel), true, v.focus == NoteCancel, v.keyboard, v.reduced);
    KeyButton(ink, l.add, AddLabel(v.status), L"Entrée", Accent(busy ? .6f : 1.f), Hex(0x00000038), {1, 1, 1, 1}, v.V(NoteAdd), !busy, v.focus == NoteAdd, v.keyboard, v.reduced, busy ? .8f : 1.f);
    ink.Stroke(all, radius, Hex(0xFFFFFF0F), 1);
}

struct ListView { std::vector<Project> projects; std::wstring selected; int hot = -1; bool keyboard = false; };
inline float ListHeight(size_t n) { using namespace NoteSpec; return 2 + 2 * listPad + n * listRow + (n ? listSep : 0) + listNew; }
inline D2D1_RECT_F ListRow(size_t n, int i) {   // i == n: « Nouveau projet… »
    using namespace NoteSpec; const float x = 1 + listPad, w = listW - 2 - 2 * listPad; float y = 1 + listPad;
    if (i < int(n)) { y += i * listRow; return {x, y, x + w, y + listRow}; }
    y += n * listRow + (n ? listSep : 0); return {x, y, x + w, y + listNew};
}
inline void PaintProjectList(Ink& ink, const ListView& v) {
    using namespace NoteSpec; const size_t n = v.projects.size(); const float h = ListHeight(n);
    ink.Fill({0, 0, listW, h}, listRadius, Hex(Ink_::raised));
    for (size_t i = 0; i <= n; ++i) {
        const auto r = ListRow(n, int(i));
        if (int(i) == v.hot) ink.Fill(r, 4, Hex(0xFFFFFF14));
        if (i < n) {
            const auto& p = v.projects[i];
            ink.Text(p.label, F(14, 20, 500), {r.left + 8, r.top + 7, r.right - 36, r.top + 27}, {1, 1, 1, 1});
            ink.Text(p.origin, F(13, 18), {r.left + 8, r.top + 28, r.right - 36, r.top + 46}, Hex(0xFFFFFF99));
            if (p.id == v.selected) { const float cy = (r.top + r.bottom) / 2; ink.Icon(Glyphs::check, {r.right - 26, cy - 8, r.right - 10, cy + 8}, {1, 1, 1, 1}); }
        } else {
            if (n) ink.Fill({r.left, r.top - 5, r.right, r.top - 4}, 0, Hex(0xFFFFFF14));
            const float cy = (r.top + r.bottom) / 2;
            ink.Icon(Glyphs::plus, {r.left + 8, cy - 8, r.left + 24, cy + 8}, {1, 1, 1, .7f});
            ink.Text(L"Nouveau projet pour cette origine…", F(14, 20), {r.left + 34, r.top + 7, r.right - 10, r.top + 27}, Hex(0xFFFFFFE6));
        }
        if (v.keyboard && int(i) == v.hot) FocusRing(ink, Inflate(r, -2), 3, true);
    }
    ink.Stroke({0, 0, listW, h}, listRadius, Hex(0xFFFFFF14), 1);
}
inline float TooltipHeight(Ink& ink) { return 2 + 16 + ink.Height(NoteSpec::tipText, F(13, 18), NoteSpec::tipW - 22); }
inline void PaintTooltip(Ink& ink, float w, float h) {
    ink.Fill({0, 0, w, h}, 6, Hex(Ink_::canvas));
    ink.Text(NoteSpec::tipText, F(13, 18), {11, 9, w - 11, h - 9}, Hex(0xFFFFFFE6), true);
    ink.Stroke({0, 0, w, h}, 6, Hex(0xFFFFFF14), 1);
}

struct NoteSubmission { std::wstring note, projectId; bool includeShot = true; };
struct NoteRequest {
    std::wstring draft; std::vector<Project> projects; std::wstring projectId; NoteContext context;
    CaptureState capture = CaptureState::Pending; bool includeShot = true, expanded = true;
};
struct NoteHooks {
    std::function<void(const NoteSubmission&)> submit;              // write off the UI thread, then Saved() or Failed()
    std::function<void(const std::wstring& draft, bool keyboard)> cancel; // Échap / Annuler: caller keeps the draft
    std::function<void()> newProject;                               // « Nouveau projet pour cette origine… »
    std::function<void(const std::wstring& projectId)> projectChanged;
    std::function<void()> blurred;                                   // the popover lost activation (stays open)
    std::function<void(const std::wstring&)> announce;
};

class NotePopover : public Animator::Client {
    static constexpr long long GroupKey = 100, RowKeyBase = 120, ListKey = 200, ListItemBase = 210;
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr; std::wstring root_; NoteHooks hooks_;
    HWND hwnd_ = nullptr, edit_ = nullptr; ID2D1HwndRenderTarget* rt_ = nullptr;
    HFONT font_ = nullptr; HBRUSH fieldBrush_ = nullptr; NativeUI::AccentCaret caret_; Accessibility::Bridge uia_;
    Layer ghost_, list_, tip_;
    NoteView view_; NoteLayout layout_; Shot shot_; std::vector<Project> projects_; std::wstring projectId_;
    Presence presence_; Motion::State motion_;
    RECT target_{}, bounds_{}, final_{}; D2D1_POINT_2F origin_{}; float liftX_ = 0, liftY_ = 0; bool above_ = false;
    bool open_ = false, settled_ = false, reduced_ = false, spinning_ = false; int hover_ = 0, pressed_ = 0, listHot_ = -1;
    static constexpr UINT_PTR EditSubclass = 0xA77E;

    float Dpi() const { const UINT d = GetDpiForWindow(hwnd_ ? hwnd_ : owner_); return d ? float(d) : 96.f; }
    int Px(float dip) const { return int(std::lround(dip * Dpi() / 96.f)); }
    Ink& I() { return *Ink::Instance(); }
    void Relayout() { view_.projectLabel = ProjectLabel(); layout_ = LayoutNote(I(), view_); }
    std::wstring ProjectLabel() const { for (auto& p : projects_) if (p.id == projectId_) return p.label; return {}; }
    std::wstring EditText() const { const int n = GetWindowTextLengthW(edit_); std::wstring s(size_t(n) + 1, 0); GetWindowTextW(edit_, s.data(), n + 1); s.resize(size_t(n)); return s; }
    COLORREF FieldGray() const { return RGB(49, 49, 49); }   // #292929 + #FFFFFF0A
    // ---- placement: below the target, else above, else beside; kept 8 DIP inside bounds.
    void Place() {
        const int w = Px(NoteSpec::width), h = Px(layout_.height), gap = Px(8), m = Px(8);
        int x = target_.left, y = target_.bottom + gap; above_ = false; liftX_ = 0; liftY_ = -Spec::popoverLift;
        if (y + h > bounds_.bottom - m) {
            if (target_.top - gap - h >= bounds_.top + m) { y = target_.top - gap - h; above_ = true; liftY_ = Spec::popoverLift; }
            else { // beside: right, else left
                y = std::clamp<int>(target_.top, bounds_.top + m, std::max<int>(bounds_.top + m, bounds_.bottom - m - h));
                if (target_.right + gap + w <= bounds_.right - m) { x = target_.right + gap; liftX_ = -Spec::popoverLift; liftY_ = 0; }
                else { x = target_.left - gap - w; liftX_ = Spec::popoverLift; liftY_ = 0; }
            }
        }
        x = std::clamp<int>(x, bounds_.left + m, std::max<int>(bounds_.left + m, bounds_.right - m - w));
        y = std::clamp<int>(y, bounds_.top + m, std::max<int>(bounds_.top + m, bounds_.bottom - m - h));
        final_ = {x, y, x + w, y + h};
        const float s = Dpi() / 96.f, cx = ((target_.left + target_.right) / 2.f - x) / s, cy = ((target_.top + target_.bottom) / 2.f - y) / s;
        origin_ = {std::clamp(cx, 0.f, NoteSpec::width), liftY_ < 0 ? 0.f : liftY_ > 0 ? layout_.height : std::clamp(cy, 0.f, layout_.height)};
        if (liftX_ < 0) origin_.x = 0; else if (liftX_ > 0) origin_.x = NoteSpec::width;
    }
    void PlaceEdit() {
        const auto& f = layout_.field;
        MoveWindow(edit_, Px(f.left + NoteSpec::fieldPadX), Px(f.top + NoteSpec::fieldPadY), Px(f.right - f.left - 2 * NoteSpec::fieldPadX), Px(f.bottom - f.top - 2 * NoteSpec::fieldPadY), TRUE);
    }
    void Resize() {   // height changed (field growth, collapse, message): keep the anchored edge
        const int oldH = final_.bottom - final_.top; Relayout(); const int h = Px(layout_.height);
        if (h == oldH) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (above_) final_.top = final_.bottom - h; else final_.bottom = final_.top + h;
        if (final_.bottom > bounds_.bottom) OffsetRect(&final_, 0, bounds_.bottom - final_.bottom);
        SetWindowPos(hwnd_, nullptr, final_.left, final_.top, final_.right - final_.left, h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (rt_) rt_->Resize(D2D1::SizeU(final_.right - final_.left, h));
        PlaceEdit(); InvalidateRect(hwnd_, nullptr, FALSE); PlaceOverlays();
        if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    // ---- painting
    void PaintReal() {
        if (!hwnd_) return;
        if (!rt_) {
            RECT c{}; GetClientRect(hwnd_, &c); const float dpi = Dpi();
            if (FAILED(I().Factory()->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi),
                D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(c.right, c.bottom)), &rt_))) return;
            rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        }
        I().Bind(rt_); rt_->BeginDraw(); rt_->Clear(D2D1::ColorF(Design::Surface::canvas, Design::Surface::canvas, Design::Surface::canvas));
        NoteView v = Visual(Motion::Now()); v.drawNote = false; PaintNote(I(), v, layout_);
        if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) { I().Forget(rt_); Release(rt_); }
    }
    NoteView Visual(Motion::Time now) {
        NoteView v = view_; v.note = EditText(); v.fieldFocused = GetFocus() == edit_ || !settled_;
        v.spinner = SpinnerAngle(now, reduced_); v.reduced = reduced_;
        for (int id = NoteField; id <= NoteAdd; ++id) v.visuals[id] = motion_.Visual(id, now);
        v.shot = shot_.width ? &shot_ : nullptr;
        return v;
    }
    void PaintGhost(Motion::Time now) {
        const Pose pose = presence_.Sample(now, reduced_, liftX_, liftY_);
        const float s = Dpi() / 96.f, lift = Spec::popoverLift;
        RECT g{final_.left - Px(lift), final_.top - Px(lift), final_.right + Px(lift), final_.bottom + Px(lift)};
        NoteView v = Visual(now); v.drawNote = true; const NoteLayout l = layout_; const D2D1_POINT_2F o = origin_;
        ghost_.paint = [v, l, pose, o, lift](Ink& ink, float, float) {
            auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Scale(pose.scale, pose.scale, o) * D2D1::Matrix3x2F::Translation(lift + pose.dx, lift + pose.dy) * base);
            ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(pose.opacity, group);
            PaintNote(ink, v, l); if (pushed) ink.PopOpacity(group);
            t->SetTransform(base);
        };
        (void)s; ghost_.Present(g, true);
    }
    // ---- overlays (list, tooltip): owned no-activate layers placed from the popover
    void PlaceOverlays() {
        if (!hwnd_) return; const float s = Dpi() / 96.f; RECT w{}; GetWindowRect(hwnd_, &w);
        if (view_.listOpen) {
            ListView lv; lv.projects = projects_; lv.selected = projectId_; lv.hot = listHot_; lv.keyboard = view_.keyboard;
            const float h = ListHeight(projects_.size());
            int x = w.left + int(std::lround(NoteSpec::listX * s)), y = w.top + int(std::lround((layout_.selector.bottom + 6) * s));
            MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
            if (y + int(h * s) > mi.rcWork.bottom) y = w.top + int(std::lround((layout_.selector.top - 6 - h) * s));
            list_.paint = [lv](Ink& ink, float, float) { PaintProjectList(ink, lv); };
            list_.Present({x, y, x + int(std::lround(NoteSpec::listW * s)), y + int(std::lround(h * s))}, true);
        } else list_.Hide();
        if (view_.tooltip && view_.expanded) {
            const float h = TooltipHeight(I());
            const int x = w.left + int(std::lround((NoteSpec::border + NoteSpec::tipX) * s)), y = w.top + int(std::lround((layout_.lineC + 1 + NoteSpec::tipY) * s));
            tip_.paint = [](Ink& ink, float tw, float th) { PaintTooltip(ink, tw, th); };
            tip_.Present({x, y, x + int(std::lround(NoteSpec::tipW * s)), y + int(std::lround(h * s))}, true);
        } else tip_.Hide();
    }
    // ---- interaction
    void Interact() { const auto now = Motion::Now(); motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now); InvalidateRect(hwnd_, nullptr, FALSE); if (motion_.NextWake(now).delay) Animator::Get().Request(this); }
    void Announce(const std::wstring& text) { if (hooks_.announce) hooks_.announce(text); else uia_.Notify(GroupKey, text.c_str(), L"AnnotationNote"); }
    void OpenList(bool keyboard) {
        if (view_.listOpen) return; view_.listOpen = true; view_.keyboard = keyboard; view_.tooltip = false;
        listHot_ = 0; for (size_t i = 0; i < projects_.size(); ++i) if (projects_[i].id == projectId_) listHot_ = int(i);
        PlaceOverlays(); InvalidateRect(hwnd_, nullptr, FALSE); if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    void CloseList() { if (!view_.listOpen) return; view_.listOpen = false; listHot_ = -1; list_.Hide(); InvalidateRect(hwnd_, nullptr, FALSE); if (uia_.Listening()) uia_.ChildrenChanged(GroupKey); }
    void Choose(int i) {
        CloseList();
        if (i >= 0 && i < int(projects_.size())) {
            projectId_ = projects_[size_t(i)].id; Relayout(); InvalidateRect(hwnd_, nullptr, FALSE);
            if (hooks_.projectChanged) hooks_.projectChanged(projectId_);
            Announce(L"Projet " + projects_[size_t(i)].label);
        } else if (i == int(projects_.size()) && hooks_.newProject) hooks_.newProject();
        if (view_.focus == 0) SetFocus(edit_);
    }
    void SetTooltip(bool on) { if (view_.tooltip == on) return; view_.tooltip = on; PlaceOverlays(); }
    void Toggle() { view_.expanded = !view_.expanded; view_.tooltip = false; tip_.Hide(); Resize(); Announce(view_.expanded ? L"Contexte affiché" : L"Contexte masqué"); }
    void Include() { if (!IncludeEnabled(view_)) return; view_.includeShot = !view_.includeShot; InvalidateRect(hwnd_, nullptr, FALSE); Announce(view_.includeShot ? L"Capture incluse" : L"Capture exclue"); }
    void Submit() {
        if (view_.status == NoteStatus::Saving) return;
        const std::wstring note = EditText();
        std::wstring problem;
        if (note.find_first_not_of(L" \t\r\n") == std::wstring::npos) problem = L"Écrivez une remarque pour l'agent.";
        else if (projectId_.empty()) problem = L"Choisissez un projet ou créez-en un pour cette origine.";
        if (!problem.empty()) {
            view_.message = problem; view_.fieldError = projectId_.empty() ? false : true; Resize();
            if (view_.fieldError) { view_.focus = 0; SetFocus(edit_); } else { view_.focus = NoteProject; SetFocus(hwnd_); }
            Announce(problem); return;
        }
        view_.status = NoteStatus::Saving; view_.message.clear(); view_.fieldError = false; Resize();
        if (hooks_.submit) hooks_.submit({note, projectId_, view_.includeShot && view_.capture != CaptureState::Unavailable});
    }
    void Cancel(bool keyboard) { const std::wstring draft = EditText(); Close(true); if (hooks_.cancel) hooks_.cancel(draft, keyboard); }
    void Activate(int hit, bool keyboard) {
        switch (hit) {
        case NoteField: SetFocus(edit_); break;
        case NoteProject: view_.listOpen ? CloseList() : OpenList(keyboard); break;
        case NoteToggle: Toggle(); break;
        case NoteInfo: SetTooltip(!view_.tooltip); break;
        case NoteInclude: Include(); break;
        case NoteCancel: Cancel(keyboard); break;
        case NoteAdd: Submit(); break;
        }
    }
    std::vector<int> Order() const {
        std::vector<int> o{NoteField, NoteProject, NoteToggle, NoteInfo};
        if (view_.expanded && IncludeEnabled(view_)) o.push_back(NoteInclude);
        o.push_back(NoteCancel); o.push_back(NoteAdd); return o;
    }
    int Current() const { return GetFocus() == edit_ ? int(NoteField) : view_.focus; }
    void FocusStop(int stop) {
        view_.keyboard = true; motion_.ClearInteractions();
        if (stop == NoteField) { view_.focus = 0; SetFocus(edit_); }
        else { view_.focus = stop; SetFocus(hwnd_); }
        SetTooltip(stop == NoteInfo);
        InvalidateRect(hwnd_, nullptr, FALSE); if (uia_.Listening()) uia_.Raise(stop == NoteField ? 0 : stop, UIA_AutomationFocusChangedEventId);
    }
    void Cycle(bool back) {
        const auto o = Order(); auto it = std::find(o.begin(), o.end(), Current());
        int i = it == o.end() ? 0 : int(it - o.begin()) + (back ? -1 : 1); i = (i + int(o.size())) % int(o.size()); FocusStop(o[size_t(i)]);
    }
    bool Key(WPARAM key, bool fromEdit) {
        const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
        if (view_.listOpen) {
            const int n = int(projects_.size()) + 1;
            switch (key) {
            case VK_ESCAPE: CloseList(); return true;
            case VK_DOWN: listHot_ = (listHot_ + 1) % n; view_.keyboard = true; PlaceOverlays(); return true;
            case VK_UP: listHot_ = (listHot_ - 1 + n) % n; view_.keyboard = true; PlaceOverlays(); return true;
            case VK_HOME: listHot_ = 0; PlaceOverlays(); return true;
            case VK_END: listHot_ = n - 1; PlaceOverlays(); return true;
            case VK_RETURN: case VK_SPACE: Choose(listHot_); return true;
            case VK_TAB: CloseList(); Cycle(shift); return true;
            }
            return true;   // the list owns the keyboard while open
        }
        if (key == VK_ESCAPE) { if (view_.tooltip) { SetTooltip(false); return true; } Cancel(true); return true; }
        if (key == VK_TAB) { Cycle(shift); return true; }
        if (key == VK_RETURN) {
            if (ctrl) { Submit(); return true; }
            if (fromEdit) { if (shift) return false; Submit(); return true; }
            if (view_.focus) { Activate(view_.focus, true); return true; }
            Submit(); return true;
        }
        if (!fromEdit && key == VK_SPACE && view_.focus) { Activate(view_.focus, true); return true; }
        if (!fromEdit && view_.focus == NoteProject && (key == VK_DOWN || key == VK_F4 || (alt && key == VK_DOWN))) { OpenList(true); return true; }
        return false;
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<NotePopover*>(data);
        switch (m) {
        case WM_GETDLGCODE: return DefSubclassProc(h, m, w, l) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN: if (self->Key(w, true)) return 0; break;
        case WM_CHAR: if (w == L'\t' || w == 27) return 0; if (w == L'\r' && GetKeyState(VK_SHIFT) >= 0) return 0; break;
        case WM_SETFOCUS: { const LRESULT r = DefSubclassProc(h, m, w, l); self->view_.focus = 0; self->caret_.Apply(h, self->FieldGray(), self->CaretColor(), std::max(1, self->Px(1)), self->Px(18)); InvalidateRect(self->hwnd_, nullptr, FALSE); return r; }
        case WM_KILLFOCUS: InvalidateRect(self->hwnd_, nullptr, FALSE); break;
        case WM_LBUTTONDOWN: self->view_.keyboard = false; break;
        }
        return DefSubclassProc(h, m, w, l);
    }
    COLORREF CaretColor() const { const auto a = Design::Accent::Get(); return NativeUI::AccentCaret::Mix(FieldGray(), RGB(std::lround(a.r * 255), std::lround(a.g * 255), std::lround(a.b * 255)), Design::Accent::caretOpacity); }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<NotePopover*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) { self = static_cast<NotePopover*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    LRESULT Handle(HWND h, UINT m, WPARAM w, LPARAM l) {
        const float s = 96.f / Dpi();
        switch (m) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintReal(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l))); return 0;
        case WM_CTLCOLOREDIT: { HDC dc = reinterpret_cast<HDC>(w); SetTextColor(dc, RGB(235, 235, 235)); SetBkColor(dc, FieldGray()); SetBkMode(dc, OPAQUE); return reinterpret_cast<LRESULT>(fieldBrush_); }
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(l) == edit_ && HIWORD(w) == EN_CHANGE) FieldChanged();
            if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE && open_) { CloseList(); SetTooltip(false); if (hooks_.blurred) hooks_.blurred(); }
            else if (LOWORD(w) != WA_INACTIVE && open_) { if (view_.focus) SetFocus(h); else SetFocus(edit_); }   // back where the user left it
            return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t);
            const int hit = NoteHitAt(layout_, view_, GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s);
            if (hit != hover_) { hover_ = hit; Interact(); SetTooltip(hit == NoteInfo || (view_.focus == NoteInfo && GetFocus() == hwnd_)); }
            return 0; }
        case WM_MOUSELEAVE: hover_ = 0; Interact(); if (view_.focus != NoteInfo) SetTooltip(false); return 0;
        case WM_LBUTTONDOWN: {
            view_.keyboard = false; const int hit = NoteHitAt(layout_, view_, GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s);
            if (hit == NoteField) { SetFocus(edit_); return 0; }
            pressed_ = hit; if (hit) { SetCapture(h); if (hit != NoteProject && GetFocus() != edit_) { view_.focus = 0; } }
            if (view_.listOpen && hit != NoteProject) CloseList();
            Interact(); return 0; }
        case WM_LBUTTONUP: {
            const int hit = NoteHitAt(layout_, view_, GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s), was = pressed_;
            pressed_ = 0; if (GetCapture() == h) ReleaseCapture(); Interact();
            if (was && was == hit) Activate(hit, false);
            return 0; }
        case WM_KEYDOWN: case WM_SYSKEYDOWN: if (Key(w, false)) return 0; break;
        case WM_SETFOCUS: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_KILLFOCUS: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_APP + 2: view_.keyboard = true; Activate(int(w), true); return 0;
        case WM_APP + 3: Choose(int(w)); return 0;
        case WM_DPICHANGED: if (rt_) { I().Forget(rt_); Release(rt_); } MakeFont(); Resize(); return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    bool ListHandle(UINT m, WPARAM, LPARAM l, LRESULT& r) {
        const float s = 1 / list_.Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        auto rowAt = [&]() { for (size_t i = 0; i <= projects_.size(); ++i) if (Inside(ListRow(projects_.size(), int(i)), x, y)) return int(i); return -1; };
        if (m == WM_MOUSEMOVE) { const int i = rowAt(); if (i != listHot_ && i >= 0) { listHot_ = i; view_.keyboard = false; PlaceOverlays(); } r = 0; return true; }
        if (m == WM_LBUTTONUP) { const int i = rowAt(); if (i >= 0) Choose(i); r = 0; return true; }
        if (m == WM_LBUTTONDOWN) { r = 0; return true; }
        return false;
    }
    // Typing (EN_CHANGE) or a draft set by Show (WM_SETTEXT on a multi-line EDIT sends no EN_CHANGE):
    // clears a validation error and grows the field (84 -> 164 DIP, then the EDIT scrolls).
    void FieldChanged() {
        if (view_.fieldError) { view_.fieldError = false; view_.message.clear(); }
        HDC dc = GetDC(edit_); auto old = SelectObject(dc, font_); TEXTMETRICW tm{}; GetTextMetricsW(dc, &tm); SelectObject(dc, old); ReleaseDC(edit_, dc);
        const int lines = int(SendMessageW(edit_, EM_GETLINECOUNT, 0, 0)); const float lineDip = (tm.tmHeight + tm.tmExternalLeading) * 96.f / Dpi();
        const float want = std::clamp(2 * NoteSpec::fieldPadY + lines * lineDip + 2, NoteSpec::fieldMin, NoteSpec::fieldMax);
        if (std::fabs(want - view_.fieldHeight) > .5f) { view_.fieldHeight = want; if (open_) Resize(); } else if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        if (open_ && !settled_) Animator::Get().Request(this);
    }
    void MakeFont() {
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Inter Display");
        if (edit_) SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    // ---- UI Automation
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> n; if (!open_) return n;
        auto screen = [&](D2D1_RECT_F r) { RECT p{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&p), 2); return p; };
        Accessibility::Node g; g.key = GroupKey; g.role = Accessibility::Role::Group; g.name = L"Nouvelle annotation"; g.automationId = L"AnnotationNote"; g.focusable = false;
        g.status = view_.status == NoteStatus::Saving ? L"Ajout en cours" : view_.message; GetWindowRect(hwnd_, &g.bounds); n.push_back(g);
        auto add = [&](int key, Accessibility::Role role, const std::wstring& name, D2D1_RECT_F r, const wchar_t* id) -> Accessibility::Node& {
            Accessibility::Node x; x.key = key; x.parent = GroupKey; x.role = role; x.name = name; x.bounds = screen(r); x.automationId = id; n.push_back(x); return n.back(); };
        auto& p = add(NoteProject, Accessibility::Role::Button, L"Projet", layout_.selector, L"AnnotationProject"); p.value = ProjectLabel(); p.help = ProjectLabel(); p.expand = view_.listOpen ? 2 : 1; p.accelerator = L"Alt+Bas";
        auto& t = add(NoteToggle, Accessibility::Role::Button, L"Ce que l'agent verra", layout_.header, L"AnnotationContextToggle"); t.expand = view_.expanded ? 2 : 1;
        auto& i = add(NoteInfo, Accessibility::Role::Button, L"À propos du contexte", layout_.info, L"AnnotationContextInfo"); i.help = NoteSpec::tipText;
        if (view_.expanded) {
            for (int k = 0; k < layout_.rowCount; ++k) {
                auto& row = add(int(RowKeyBase) + k, Accessibility::Role::Text, layout_.rows[k].label, layout_.rows[k].value, L"AnnotationContextRow"); row.focusable = false;
                row.value = RowValue(view_.context, layout_.rows[k].index);
            }
            auto& c = add(NoteInclude, Accessibility::Role::Button, L"Inclure la capture", layout_.include, L"AnnotationIncludeShot");
            c.toggle = view_.includeShot && IncludeEnabled(view_) ? 2 : 1; c.enabled = IncludeEnabled(view_);
            c.help = view_.capture == CaptureState::Pending ? L"Capture en cours" : view_.capture == CaptureState::Unavailable ? L"Capture indisponible" : view_.context.shotLabel;
        }
        auto& cancel = add(NoteCancel, Accessibility::Role::Button, L"Annuler", layout_.cancel, L"AnnotationCancel"); cancel.accelerator = L"Échap";
        auto& ok = add(NoteAdd, Accessibility::Role::Button, AddLabel(view_.status), layout_.add, L"AnnotationAdd"); ok.accelerator = L"Entrée"; ok.enabled = view_.status != NoteStatus::Saving;
        return n;
    }
    std::vector<Accessibility::Node> ListNodes() {
        std::vector<Accessibility::Node> n; if (!view_.listOpen) return n;
        Accessibility::Node l; l.key = ListKey; l.role = Accessibility::Role::List; l.name = L"Projets"; l.focusable = false; GetWindowRect(list_.Window(), &l.bounds); n.push_back(l);
        for (size_t i = 0; i <= projects_.size(); ++i) {
            Accessibility::Node it; it.key = ListItemBase + static_cast<long long>(i); it.parent = ListKey; it.role = Accessibility::Role::ListItem;
            it.name = i < projects_.size() ? projects_[i].label : L"Nouveau projet pour cette origine…"; if (i < projects_.size()) it.help = projects_[i].origin;
            it.selected = i < projects_.size() && projects_[i].id == projectId_; it.position = int(i) + 1; it.count = int(projects_.size()) + 1;
            it.bounds = list_.Screen(ListRow(projects_.size(), int(i))); n.push_back(it);
        }
        return n;
    }
    bool Create() {
        if (hwnd_) return true;
        if (!Ink::Shared(root_)) return false;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance_; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserAnnotationNote";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"Nouvelle annotation", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, owner_, nullptr, instance_, this);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        NativeUI::ConfigureShadowlessPopup(hwnd_, NoteSpec::radius);
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 1, 1, hwnd_, nullptr, instance_, nullptr);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0); SendMessageW(edit_, EM_SETLIMITTEXT, 4000, 0);
        SetWindowSubclass(edit_, EditProc, EditSubclass, reinterpret_cast<DWORD_PTR>(this));
        fieldBrush_ = CreateSolidBrush(FieldGray()); MakeFont();
        Layer::Options ghost; ghost.clickThrough = true; ghost.title = L"";
        Layer::Options pop; pop.title = L"Projets";
        Layer::Options tip; tip.clickThrough = true; tip.title = L"Info-bulle";
        if (!ghost_.Create(instance_, owner_, ghost) || !list_.Create(instance_, hwnd_, pop) || !tip_.Create(instance_, hwnd_, tip)) return false;
        list_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return ListHandle(m, w, l, r); };
        Accessibility::Host lh; lh.window = list_.Window(); lh.nodes = [this] { return ListNodes(); };
        lh.focused = [this]() -> long long { return view_.listOpen && listHot_ >= 0 ? ListItemBase + listHot_ : 0; };
        lh.select = [this](long long key) { const int i = int(key - ListItemBase); PostMessageW(hwnd_, WM_APP + 3, WPARAM(i), 0); };
        lh.invoke = lh.select; list_.uia.Attach(std::move(lh));
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? view_.focus : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 2, WPARAM(key), 0); };
        host.toggle = host.invoke;
        host.expand = [this](long long key, bool) { PostMessageW(hwnd_, WM_APP + 2, WPARAM(key), 0); };
        host.label = [this](HWND child, Accessibility::Node& node) {
            if (child != edit_) return false;
            node.name = L"Remarque pour l'agent"; node.automationId = L"AnnotationNoteField"; node.accelerator = L"Entrée pour ajouter, Maj+Entrée pour une nouvelle ligne";
            if (view_.fieldError) node.help = view_.message; return true; };
        uia_.Attach(std::move(host));
        motion_.EnablePolish();
        return true;
    }
    void Settle() {
        settled_ = true; ghost_.Hide();
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA); InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_);
        PlaceOverlays();
    }
public:
    ~NotePopover() { Dispose(); }
    void Dispose() {
        if (Animator::Exists()) Animator::Get().Forget(this);
        uia_.Detach(); if (rt_ && Ink::Instance()) Ink::Instance()->Forget(rt_); Release(rt_);
        ghost_.Destroy(); list_.Destroy(); tip_.Destroy();
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
        if (font_) DeleteObject(font_); if (fieldBrush_) DeleteObject(fieldBrush_); font_ = nullptr; fieldBrush_ = nullptr; hwnd_ = edit_ = nullptr; open_ = false;
    }
    void Init(HINSTANCE instance, HWND owner, std::wstring root, NoteHooks hooks) { instance_ = instance; owner_ = owner; root_ = std::move(root); hooks_ = std::move(hooks); }
    bool Created() const { return hwnd_ != nullptr; }
    bool Visible() const { return open_; }
    HWND Window() const { return hwnd_; }
    HWND Edit() const { return edit_; }
    // target: the element rect, bounds: the page area (both screen px).
    bool Show(const NoteRequest& request, RECT target, RECT bounds, bool keyboard, bool reduced) {
        if (!Create()) return false;
        const auto now = Motion::Now();
        projects_ = request.projects; projectId_ = request.projectId; if (projectId_.empty() && projects_.size() == 1) projectId_ = projects_[0].id;
        view_ = NoteView{}; view_.context = request.context; view_.capture = request.capture; view_.includeShot = request.includeShot; view_.expanded = request.expanded; view_.keyboard = keyboard;
        if (request.capture != CaptureState::Ready) shot_ = Shot{};
        reduced_ = reduced; target_ = target; bounds_ = bounds; hover_ = pressed_ = 0; listHot_ = -1; motion_.ClearInteractions(); motion_.SetReducedMotion(reduced);
        open_ = false; SetWindowTextW(edit_, request.draft.c_str()); SendMessageW(edit_, EM_SETSEL, WPARAM(request.draft.size()), LPARAM(request.draft.size()));
        PlaceEdit(); FieldChanged(); view_.fieldError = false; view_.message.clear();
        Relayout(); Place();
        SetWindowPos(hwnd_, HWND_TOP, final_.left, final_.top, final_.right - final_.left, final_.bottom - final_.top, SWP_NOACTIVATE);
        if (rt_) rt_->Resize(D2D1::SizeU(final_.right - final_.left, final_.bottom - final_.top));
        PlaceEdit();
        const bool wasOpen = open_ && presence_.Progress(now) > 0;
        open_ = true; settled_ = false;
        presence_.Show(now);
        SetLayeredWindowAttributes(hwnd_, 0, reduced_ ? BYTE(std::lround(255 * presence_.Progress(now))) : 0, LWA_ALPHA);
        ShowWindow(hwnd_, SW_SHOW); EnableWindow(hwnd_, TRUE);
        SetForegroundWindow(hwnd_); SetFocus(edit_);
        if (!reduced_) PaintGhost(now);
        Animator::Get().Request(this);
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); uia_.Raise(GroupKey, UIA_Window_WindowOpenedEventId); }
        (void)wasOpen; return true;
    }
    void SetShot(CaptureState state, const Shot* shot) {
        view_.capture = state; if (shot && state == CaptureState::Ready) shot_ = *shot; else if (state != CaptureState::Ready) shot_ = Shot{};
        if (state == CaptureState::Unavailable) view_.includeShot = false;
        if (!hwnd_) return; InvalidateRect(hwnd_, nullptr, FALSE); if (state == CaptureState::Pending) Animator::Get().Request(this);
    }
    void SetProjects(std::vector<Project> projects, const std::wstring& selected) {
        projects_ = std::move(projects); projectId_ = selected; if (view_.message.find(L"projet") != std::wstring::npos) view_.message.clear();
        if (!hwnd_) return; Resize(); PlaceOverlays();
    }
    // The write finished: close (the card « Annotation ajoutée » is the launcher's job).
    void Saved() { view_.status = NoteStatus::Editing; Close(true); }
    // The write failed: keep the note, say so, offer « Réessayer » (focus on it).
    void Failed(const std::wstring& message = L"Impossible d'enregistrer l'annotation. Votre note est conservée ; réessayez.") {
        if (!hwnd_) return; view_.status = NoteStatus::Failed; view_.message = message; Resize();
        view_.focus = NoteAdd; SetFocus(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE); Announce(message);
    }
    void Close(bool animate = true) {
        if (!open_ || !hwnd_) return;
        const auto now = Motion::Now(); open_ = false; CloseList(); SetTooltip(false); tip_.Hide();
        const bool ghost = animate && !reduced_ && presence_.Progress(now) > 0;
        presence_.Hide(now, animate);
        if (GetForegroundWindow() == hwnd_ && owner_) SetForegroundWindow(owner_);
        if (ghost) { ShowWindow(hwnd_, SW_HIDE); PaintGhost(now); }
        else if (reduced_ && animate) EnableWindow(hwnd_, FALSE);
        else { ShowWindow(hwnd_, SW_HIDE); ghost_.Hide(); }
        if (uia_.Listening()) uia_.Raise(GroupKey, UIA_Window_WindowClosedEventId);
        if (presence_.Moving(now)) Animator::Get().Request(this);
    }
    bool Frame(Motion::Time now) override {
        if (!hwnd_) return false;
        const bool moving = presence_.Moving(now);
        if (open_) {
            if (reduced_) { SetLayeredWindowAttributes(hwnd_, 0, BYTE(std::lround(255 * presence_.Progress(now))), LWA_ALPHA); if (!moving && !settled_) Settle(); }
            else if (moving) PaintGhost(now);
            else if (!settled_) Settle();
        } else {
            if (reduced_) { SetLayeredWindowAttributes(hwnd_, 0, BYTE(std::lround(255 * presence_.Progress(now))), LWA_ALPHA); if (!moving) { ShowWindow(hwnd_, SW_HIDE); EnableWindow(hwnd_, TRUE); } }
            else if (moving) PaintGhost(now);
            else ghost_.Hide();
        }
        const bool spin = open_ && settled_ && view_.expanded && view_.capture == CaptureState::Pending && !reduced_;
        if (spin || (open_ && settled_ && motion_.NextWake(now).delay)) InvalidateRect(hwnd_, nullptr, FALSE);
        return moving || spin || (open_ && motion_.NextWake(now).delay.has_value());
    }
    // ---- tests / QA
    const NoteView& ViewForTest() const { return view_; }
    const NoteLayout& LayoutForTest() const { return layout_; }
    Presence& PresenceForTest() { return presence_; }
    Layer& GhostForTest() { return ghost_; }
    Layer& ListForTest() { return list_; }
    Layer& TipForTest() { return tip_; }
    bool SettledForTest() const { return settled_; }
    bool KeyForTest(WPARAM key, bool fromEdit) { return Key(key, fromEdit); }
    void TypeForTest(const std::wstring& text) { SetWindowTextW(edit_, text.c_str()); FieldChanged(); }
    void ActivateForTest(int hit) { Activate(hit, true); }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
    std::vector<Accessibility::Node> ListNodesForTest() { return ListNodes(); }
    RECT FinalForTest() const { return final_; }
    D2D1_POINT_2F OriginForTest() const { return origin_; }
    float LiftYForTest() const { return liftY_; }
    int FocusForTest() const { return Current(); }
};
}} // namespace NativeChrome::Annotations
