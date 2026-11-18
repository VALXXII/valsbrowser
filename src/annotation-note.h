#pragma once
#include "i18n.h"
// Note panel (V2) — Paper « MCP - Design UI UX »: 2.1 1075-0 (écrire la note), 2.2 10D3-0 (« Ce que
// l'agent verra » déplié), 2.3 10NN-0 (capture impossible); motion « Spéc · Annotations » 10IT-0.
//   Panel 350, canvas #171717, border 1 (#3E67FF99 = the window accent at 60 % while the field has focus, else #FFFFFF1F),
//   radius 12, padding 12, gap 12, min-height 84:
//     native multi-line EDIT, 14/20 #FFFFFFE6, placeholder « Que faut-il changer ? » #FFFFFF99
//     (also the field's accessible name);
//     row h 24: site (globe 13 at 50 %, gap 6, padding-inline 2, origin 12/16 #FFFFFF99, not
//     interactive) and « Ajouter » (accent, h 24, radius 6, padding 10, 13/16 medium white).
//     Chat intégré (local sites): two actions, the main one LAST (right) and the only one filled with the accent:
//     « Ajouter » (secondary: neutral plate #FFFFFF0F, padding 8) then, 6 further, « Demander à l'agent » (accent,
//     padding 10). Entrée = the main action: « Demander à l'agent » when it is there, « Ajouter » otherwise;
//     Ctrl+Entrée = « Ajouter » always; Ctrl+Maj+D = « Demander à l'agent ».
//   Separate pill 8 under the panel, centred: canvas, border #FFFFFF1F, radius 8, padding 6/8,
//     gap 6: « Ce que l'agent verra » 12/16 #FFFFFF99 + caret 14 at 60 %. Unfolded (2.2): 350,
//     radius 12, padding 10/12/12, gap 10: centred header 12/16 medium #FFFFFFE6 + caret up;
//     capture 96x62 radius 6 (outline #FFFFFF1A), gap 12, rows gap 5: label w 48 12/16 #FFFFFF99,
//     gap 8, value 12/16 #FFFFFFE6 (Page, Élément, Style).
//     Capture impossible (2.3): warning-circle 13 at 80 % + « Capture impossible ici ·
//     l'annotation partira sans image » 12/16 #FFFFFFCC (neutral, not interactive).
// No project selector any more: annotations are grouped by site (the origin), shown, not chosen.
// Motion: the panel is born next to the clicked element (origin on the target side): scale
// 0.96 -> 1 + opacity, 180 ms cubic-bezier(.23,1,.32,1), through a click-through ghost layer
// while the field's window (EDIT focused from frame 0) stays at alpha 0; the pill follows 60 ms
// later (opacity + 4 DIP) and unfolds downward into its card in 200 ms. « Ajouter »: the panel
// folds toward the launcher (bottom-right) in 150 ms. Reduced motion: fades only.
// Windows (smooth corners): the panel is a per-pixel-alpha Layer like every other annotation surface: its
// rounded corners and its 1 DIP border are antialiased against whatever is behind. It has NO window region
// (a region is a 1-bit mask: the stair-stepped corners of the first version) and no DWM rounding (which always
// brings a shadow, native-ui.h). A layer cannot host a child window, so the native EDIT lives in a second,
// plain rectangular window exactly on the panel's field (13 DIP inside the edge: never under a corner), owned
// by the panel so it always sits above it. That window holds the keyboard focus; the panel is never activated.
#include "annotation-kit.h"
#include "native-ui.h"
#include <commctrl.h>

namespace NativeChrome { namespace Annotations {
struct Project { std::wstring id, label, origin; };   // V1 (deprecated: no project selector in V2)
struct NoteContext {
    std::wstring page, element, text, styles, shotLabel;   // V1 fields (page = URL, element = selector)
    std::wstring site;    // V2: origin shown in the panel (« localhost:5173 »); empty = derived from page
    std::wstring label;   // V2: human element (« Bouton « Payer 48,00 € » »); empty = element
    std::wstring style;   // V2: style summary (« Taille, couleurs et marges »); empty = default when styles exist
};
enum class CaptureState { Pending, Ready, Unavailable };
enum class NoteStatus { Editing, Saving, Failed };
enum NoteHit { NoteNone = 0, NoteField = 1, NoteAdd = 2, NotePill = 3, NoteAsk = 4 };   // NoteAsk: « Demander à l'agent » (chat intégré, local sites only)

namespace NoteSpec {
inline constexpr float width = 350, radius = 12, pad = 12, gap = 12, row = 24, minHeight = 84, noteLine = 20;
inline constexpr float fieldMin = 22, fieldMax = 160;
inline constexpr float pillGap = 8, pillRadius = 8, cardRadius = 12, labelW = 48;
inline constexpr float thumbW = 96, thumbH = 62;      // capture thumbnail box (V3: the image is contained, never cropped)
// V3 (retours §11, Paper 5.7): an image wider than 2.2:1 would be a thin strip in that box: the card stacks instead,
// the image full width (326 x 64, contain, neutral page colour) above Page / Élément / Style.
inline constexpr float wideRatio = 2.2f, wideH = 64;
inline const wchar_t* placeholder() { return TR(note_placeholder); }
inline const wchar_t* pillLabel() { return TR(note_pill_label); }
inline const wchar_t* unavailable() { return TR(note_capture_unavailable); }
}
// « localhost:5173 » from « http://localhost:5173/checkout » (or the page itself if it has no scheme).
inline std::wstring SiteOf(const std::wstring& page) {
    std::wstring s = page; const size_t scheme = s.find(L"://"); if (scheme != std::wstring::npos) s = s.substr(scheme + 3);
    const size_t slash = s.find_first_of(L"/?#"); return slash == std::wstring::npos ? s : s.substr(0, slash);
}
inline std::wstring NoteSite(const NoteContext& c) { return c.site.empty() ? SiteOf(c.page) : c.site; }
inline std::wstring NoteElement(const NoteContext& c) { return c.label.empty() ? c.element : c.label; }
inline std::wstring NoteStyle(const NoteContext& c) { return !c.style.empty() ? c.style : c.styles.empty() ? std::wstring() : std::wstring(TR(note_style_summary)); }
inline std::wstring NotePage(const NoteContext& c) { std::wstring s = c.page; const size_t scheme = s.find(L"://"); return scheme == std::wstring::npos ? s : s.substr(scheme + 3); }

struct NoteView {
    std::wstring note; bool drawNote = true, fieldFocused = true, fieldError = false; float fieldHeight = 40;
    NoteContext context; CaptureState capture = CaptureState::Ready; const Shot* shot = nullptr;
    NoteStatus status = NoteStatus::Editing; std::wstring message;
    bool ask = false;   // chat intégré: « Demander à l'agent », the main action, right of « Ajouter » (then secondary)
    int focus = 0; bool keyboard = false, reduced = false; float spinner = 0;
    std::map<int, Motion::InteractionVisual> visuals;
    Motion::InteractionVisual V(int id) const { auto it = visuals.find(id); return it == visuals.end() ? Motion::InteractionVisual{} : it->second; }
};
struct NoteLayout { float height = 0; D2D1_RECT_F field{}, row{}, site{}, add{}, message{}, ask{}; };
inline std::wstring AddLabel(NoteStatus s) { return s == NoteStatus::Saving ? TR(note_adding) : s == NoteStatus::Failed ? TR(downloads_action_retry) : TR(note_add); }
inline NoteLayout LayoutNote(Ink& ink, const NoteView& v) {
    using namespace NoteSpec; NoteLayout l; const float x0 = 1 + pad, x1 = width - 1 - pad;
    float y = 1 + pad;
    const float fh = std::max(v.fieldHeight, minHeight - 2 - 2 * pad - gap - row);
    l.field = {x0, y, x1, y + fh}; y += fh + gap;
    l.row = {x0, y, x1, y + row};
    // Actions end the row, the main one last. Padding 10 for the filled (accent) button, 8 for the neutral one.
    const float addPad = v.ask ? 8.f : 10.f, aw = addPad + ink.Width(AddLabel(v.status), F(13, 16, 500)) + addPad;
    float addRight = x1;
    if (v.ask) { const float kw = 10 + ink.Width(TR(chat_note_ask), F(13, 16, 500)) + 10; l.ask = {x1 - kw, y, x1, y + row}; addRight = l.ask.left - 6; }
    l.add = {addRight - aw, y, addRight, y + row};
    l.site = {x0, y, std::max(x0, l.add.left - 8), y + row}; y += row;
    if (!v.message.empty()) { y += 8; const float mh = ink.Height(v.message, F(12, 16), x1 - x0 - 19); l.message = {x0, y, x1, y + mh}; y += mh; }
    l.height = y + pad + 1; return l;
}
inline int NoteHitAt(const NoteLayout& l, float x, float y) {
    if (Inside(l.add, x, y)) return NoteAdd;
    if (l.ask.right > l.ask.left && Inside(l.ask, x, y)) return NoteAsk;
    if (Inside(l.field, x, y)) return NoteField;
    return NoteNone;
}
inline void PaintNote(Ink& ink, const NoteView& v, const NoteLayout& l) {
    using namespace NoteSpec;
    const D2D1_RECT_F all{0, 0, width, l.height};
    ink.Fill(all, radius, Hex(Ink_::canvas));
    if (v.drawNote) {
        auto* t = ink.Target(); t->PushAxisAlignedClip(l.field, D2D1_ANTIALIAS_MODE_ALIASED);
        if (v.note.empty()) ink.Text(placeholder(), F(14, 20), l.field, Hex(0xFFFFFF99), true);
        else ink.Text(v.note, F(14, 20), l.field, Hex(0xFFFFFFE6), true);
        t->PopAxisAlignedClip();
    }
    Pieces(ink, {PIcon(Glyphs::globe, 13, {1, 1, 1, .5f}), PText(NoteSite(v.context), F(12, 16), Hex(0xFFFFFF99))}, {l.site.left + 2, l.site.top, l.site.right, l.site.bottom}, Align::Leading);
    // One filled (accent) button: the main action. « Ajouter » alone is that one; next to « Demander à l'agent »
    // it becomes the secondary action (neutral plate, same height and radius).
    const bool busy = v.status == NoteStatus::Saving;
    {   const auto iv = v.V(NoteAdd);
        ScaleScope s(ink.Target(), l.add, Motion::PressScale(iv.press, v.reduced, !busy));
        if (v.ask) ink.Fill(l.add, 6, {1, 1, 1, busy ? .06f : .06f + .06f * iv.hover + .04f * iv.press});
        else ink.Fill(l.add, 6, busy ? Accent(.6f) : AccentButton(iv.hover, iv.press));
        ink.Text(AddLabel(v.status), F(13, 16, 500), l.add, {1, 1, 1, busy ? (v.ask ? .6f : .8f) : (v.ask ? .9f : 1.f)}, false, Align::Center, true);
        if (v.focus == NoteAdd) FocusRing(ink, l.add, 6, v.keyboard); }
    if (v.ask) {   // unavailable while the note is being written (dimmed, no press)
        const auto iv = v.V(NoteAsk);
        ScaleScope s(ink.Target(), l.ask, Motion::PressScale(iv.press, v.reduced, !busy));
        ink.Fill(l.ask, 6, busy ? Accent(.45f) : AccentButton(iv.hover, iv.press));
        ink.Text(TR(chat_note_ask), F(13, 16, 500), l.ask, {1, 1, 1, busy ? .6f : 1.f}, false, Align::Center, true);
        if (v.focus == NoteAsk) FocusRing(ink, l.ask, 6, v.keyboard); }
    if (!v.message.empty()) {
        ink.Icon(Glyphs::warningCircle, {l.message.left, l.message.top + 1.5f, l.message.left + 13, l.message.top + 14.5f}, Hex(Ink_::danger));
        ink.Text(v.message, F(12, 16), {l.message.left + 19, l.message.top, l.message.right, l.message.bottom}, Hex(Ink_::danger), true);
    }
    const D2D1_COLOR_F border = v.fieldError ? WithAlpha(Hex(Ink_::danger), .6f) : v.fieldFocused ? Accent(.6f) : Hex(0xFFFFFF1F);
    ink.Stroke(all, radius, border, 1);
}

// ---- the « Ce que l'agent verra » pill: fold 0 (pill) .. 1 (card) --------------------------
inline bool PillExpandable(CaptureState c) { return c != CaptureState::Unavailable; }
inline D2D1_SIZE_F PillCollapsed(Ink& ink, CaptureState c) {
    // Never wider than the note (its layer): a longer label ends in an ellipsis inside the pill.
    if (!PillExpandable(c)) return {std::min(NoteSpec::width, 8 + 13 + 6 + ink.Width(NoteSpec::unavailable(), F(12, 16)) + 8 + 2), 30};
    return {std::min(NoteSpec::width, 8 + ink.Width(NoteSpec::pillLabel(), F(12, 16)) + 6 + 14 + 8 + 2), 30};
}
inline bool PillWide(const Shot* shot) { return shot && shot->width && shot->height && float(shot->width) / float(shot->height) > NoteSpec::wideRatio; }
inline float PillExpandedH(bool wide = false) {
    return wide ? 1 + 10 + 16 + 10 + NoteSpec::wideH + 10 + (3 * 16 + 2 * 5) + 12 + 1 : 1 + 10 + 16 + 10 + NoteSpec::thumbH + 12 + 1;
}
inline D2D1_RECT_F PillRect(Ink& ink, CaptureState c, float cx, float top, float fold, bool wide = false) {
    const auto s = PillCollapsed(ink, c); const float w = Lerp(s.width, NoteSpec::width, fold), h = Lerp(s.height, PillExpandedH(wide), fold);
    return {cx - w / 2, top, cx + w / 2, top + h};
}
struct PillView { NoteContext context; CaptureState capture = CaptureState::Ready; const Shot* shot = nullptr; float fold = 0, spinner = 0; bool focus = false, keyboard = false; Motion::InteractionVisual iv; };
inline void PaintCaptureBox(Ink& ink, const Shot* shot, CaptureState capture, D2D1_RECT_F box, float radius, float spinner) {
    if (capture == CaptureState::Ready && shot && shot->width) ink.Thumbnail(shot, box, radius, 1, true);   // V3: contain
    else ink.Fill(box, radius, capture == CaptureState::Unavailable ? Hex(0xFFFFFF0A) : Hex(Ink_::page));
    const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
    if (capture == CaptureState::Pending) ink.Icon(Glyphs::ring, {cx - 8, cy - 8, cx + 8, cy + 8}, Accent(), spinner);
    if (capture == CaptureState::Unavailable) ink.Icon(Glyphs::warningCircle, {cx - 8, cy - 8, cx + 8, cy + 8}, {1, 1, 1, .5f});
    ink.Stroke(box, radius, Hex(0xFFFFFF1A), 1);
}
inline void PaintPill(Ink& ink, const PillView& v, D2D1_RECT_F r) {
    using namespace NoteSpec;
    const float radius = Lerp(pillRadius, cardRadius, v.fold);
    ink.Fill(r, radius, Hex(Ink_::canvas));
    if (v.iv.hover > 0 && v.fold < 1) ink.Fill(r, radius, {1, 1, 1, .04f * v.iv.hover * (1 - v.fold)});
    const float a = Motion::Clamp(1 - v.fold * 2.5f);
    if (a > 0) {
        ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(a, g);
        const D2D1_RECT_F head{r.left, r.top, r.right, r.top + 30};
        const D2D1_RECT_F inside{head.left + 1 + 7, head.top, head.right - 1 - 7, head.bottom};   // the pill's 8 DIP padding (1 DIP of rounding slack)
        if (PillExpandable(v.capture)) Pieces(ink, {PText(pillLabel(), F(12, 16), Hex(0xFFFFFF99)), PIcon(Glyphs::caretDown, 14, {1, 1, 1, .6f})}, inside);
        else Pieces(ink, {PIcon(Glyphs::warningCircle, 13, {1, 1, 1, .8f}), PText(unavailable(), F(12, 16), Hex(0xFFFFFFCC))}, inside);
        if (pushed) ink.PopOpacity(g);
    }
    if (v.fold > 0) {
        ID2D1Layer* layer = nullptr; ID2D1Geometry* clip = nullptr; PushRoundClip(ink, r, radius, layer, clip);
        const float b = Motion::Clamp(v.fold * 2 - .5f);
        ID2D1Layer* g = nullptr; const bool pushed = ink.PushOpacity(b, g);
        const float top = r.top + 11, x = r.left + 13;
        Pieces(ink, {PText(pillLabel(), F(12, 16, 500), Hex(0xFFFFFFE6)), PIcon(Glyphs::caretDown, 14, {1, 1, 1, .6f}, 6, 180)}, {r.left, top, r.right, top + 16});
        const float cy = top + 16 + 10; const bool wide = PillWide(v.shot);
        float rx = x + thumbW + 12, ry = cy;
        if (wide) { PaintCaptureBox(ink, v.shot, v.capture, {x, cy, r.right - 13, cy + wideH}, 6, v.spinner); rx = x; ry = cy + wideH + 10; }
        else PaintCaptureBox(ink, v.shot, v.capture, {x, cy, x + thumbW, cy + thumbH}, 6, v.spinner);
        const std::wstring values[3] = {NotePage(v.context), NoteElement(v.context), NoteStyle(v.context)};
        const wchar_t* labels[3] = {TR(note_ctx_page), TR(note_ctx_element), TR(note_ctx_style)};
        for (int i = 0; i < 3; ++i) {
            ink.Text(labels[i], F(12, 16), {rx, ry, rx + labelW, ry + 16}, Hex(0xFFFFFF99));
            ink.Text(values[i], F(12, 16), {rx + labelW + 8, ry, r.right - 13, ry + 16}, Hex(0xFFFFFFE6));
            ry += 16 + 5;
        }
        if (pushed) ink.PopOpacity(g);
        PopClip(ink, layer, clip);
    }
    ink.Stroke(r, radius, Hex(0xFFFFFF1F), 1);
    if (v.focus) FocusRing(ink, r, radius, v.keyboard);
}

// ---- V1 helpers kept for the deprecated onboarding surface (annotation-onboarding.h) ----
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

struct NoteSubmission { std::wstring note, projectId; bool includeShot = true; std::wstring site; };
struct NoteRequest {
    std::wstring draft; NoteContext context; CaptureState capture = CaptureState::Pending;
    bool expanded = false;                                   // V2: the pill starts unfolded
    std::vector<Project> projects; std::wstring projectId;   // V1, ignored (kept so older callers compile)
    bool includeShot = true;                                 // V1, ignored: the capture is always included
    bool askAgent = false;                                   // chat intégré: show « Demander à l'agent »
};
struct NoteHooks {
    std::function<void(const NoteSubmission&)> submit;              // write off the UI thread, then Saved() or Failed()
    std::function<void(const std::wstring& draft, bool keyboard)> cancel; // Échap: the caller keeps the draft
    std::function<void()> blurred;                                   // the panel lost activation (stays open)
    std::function<void(const std::wstring&)> announce;
    std::function<void()> newProject;                                // V1, never called
    std::function<void(const std::wstring&)> projectChanged;         // V1, never called
    std::function<void(const std::wstring& draft)> ask;              // « Demander à l'agent »: the caller moves the selection to the chat and closes the panel
};

class NotePopover : public Animator::Client {
    static constexpr long long GroupKey = 100, SiteKey = 101, PillRowBase = 120, ShotKey = 130;
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr; std::wstring root_; NoteHooks hooks_;
    HWND hwnd_ = nullptr, edit_ = nullptr;   // hwnd_: the FIELD's window (plain rectangle on the panel's field), parent of the EDIT
    HFONT font_ = nullptr; HBRUSH fieldBrush_ = nullptr; NativeUI::AccentCaret caret_; Accessibility::Bridge uia_;
    Layer ghost_, pill_, panel_;   // panel_: the note itself once settled (per-pixel alpha: smooth corners, buttons, border)
    NoteView view_; NoteLayout layout_; Shot shot_;
    Presence presence_{NoteTiming()}; Ramp pillIn_; Ramp fold_; Motion::State motion_;
    RECT target_{}, bounds_{}, final_{}; D2D1_POINT_2F origin_{}; float liftX_ = 0, liftY_ = 0; bool above_ = false;
    bool open_ = false, settled_ = false, reduced_ = false, pillOpen_ = false; int hover_ = 0, pressed_ = 0, pillHover_ = 0, pillPressed_ = 0;
    static constexpr UINT_PTR EditSubclass = 0xA77E;

    float Dpi() const { const UINT d = GetDpiForWindow(hwnd_ ? hwnd_ : owner_); return d ? float(d) : 96.f; }
    int Px(float dip) const { return int(std::lround(dip * Dpi() / 96.f)); }
    Ink& I() { return *Ink::Instance(); }
    void Relayout() { layout_ = LayoutNote(I(), view_); }
    std::wstring EditText() const { const int n = GetWindowTextLengthW(edit_); std::wstring s(size_t(n) + 1, 0); GetWindowTextW(edit_, s.data(), n + 1); s.resize(size_t(n)); return s; }
    static COLORREF FieldGray() { return RGB(23, 23, 23); }        // canvas #171717
    static COLORREF TextGray() { return RGB(232, 232, 232); }      // #FFFFFFE6 on canvas
    static COLORREF HintGray() { return RGB(162, 162, 162); }      // #FFFFFF99 on canvas
    // ---- placement: beside the clicked element (right, else left), else below, else above; 8 DIP inside the page.
    void Place() {
        const int w = Px(NoteSpec::width), h = Px(layout_.height), gap = Px(8), m = Px(8);
        int x, y = target_.top; above_ = false;
        if (target_.right + gap + w <= bounds_.right - m) { x = target_.right + gap; liftX_ = -Spec::popoverLift; liftY_ = 0; }
        else if (target_.left - gap - w >= bounds_.left + m) { x = target_.left - gap - w; liftX_ = Spec::popoverLift; liftY_ = 0; }
        else if (target_.bottom + gap + h <= bounds_.bottom - m) { x = target_.left; y = target_.bottom + gap; liftX_ = 0; liftY_ = -Spec::popoverLift; }
        else { x = target_.left; y = target_.top - gap - h; above_ = true; liftX_ = 0; liftY_ = Spec::popoverLift; }
        x = std::clamp<int>(x, bounds_.left + m, std::max<int>(bounds_.left + m, bounds_.right - m - w));
        y = std::clamp<int>(y, bounds_.top + m, std::max<int>(bounds_.top + m, bounds_.bottom - m - h));
        final_ = {x, y, x + w, y + h};
        const float s = Dpi() / 96.f, cy = ((target_.top + target_.bottom) / 2.f - y) / s;
        if (liftX_ < 0) origin_ = {0, std::clamp(cy, 0.f, layout_.height)};
        else if (liftX_ > 0) origin_ = {NoteSpec::width, std::clamp(cy, 0.f, layout_.height)};
        else origin_ = {std::clamp(((target_.left + target_.right) / 2.f - x) / s, 0.f, NoteSpec::width), liftY_ < 0 ? 0.f : layout_.height};
    }
    // The field's window sits exactly on the panel's field (screen px); the EDIT fills it.
    RECT FieldScreen() const { const auto& f = layout_.field; return {final_.left + Px(f.left), final_.top + Px(f.top), final_.left + Px(f.right), final_.top + Px(f.bottom)}; }
    void PlaceField() {
        const RECT r = FieldScreen(); const int w = std::max<int>(1, r.right - r.left), h = std::max<int>(1, r.bottom - r.top);
        SetWindowPos(hwnd_, nullptr, r.left, r.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        MoveWindow(edit_, 0, 0, w, h, TRUE);
    }
    // The settled panel: everything but the note's text (the EDIT draws it), in ONE layered update (place, size, pixels).
    void PresentPanel(bool raise = false) {
        NoteView v = Visual(Motion::Now()); v.drawNote = false; const NoteLayout l = layout_;
        panel_.paint = [v, l](Ink& ink, float, float) { PaintNote(ink, v, l); };
        panel_.Present(final_, raise);
    }
    void Repaint() { if (hwnd_ && open_ && settled_) PresentPanel(); }   // while it animates, the ghost paints (Frame)
    void Resize() {   // height changed (field growth, message): keep the anchored edge
        const int oldH = final_.bottom - final_.top; Relayout(); const int h = Px(layout_.height);
        if (h == oldH) { Repaint(); PlacePill(Motion::Now()); return; }
        if (above_) final_.top = final_.bottom - h; else final_.bottom = final_.top + h;
        if (final_.bottom > bounds_.bottom) OffsetRect(&final_, 0, bounds_.bottom - final_.bottom);
        // V3 (no flash): the field's window never sticks out of the panel: growing, the panel first (its field area is
        // the canvas colour); shrinking, the field first. Both are painted at once (no frame of uninitialised pixels).
        if (h > oldH) { Repaint(); PlaceField(); } else { PlaceField(); Repaint(); }
        if (settled_ && open_) UpdateWindow(hwnd_);
        PlacePill(Motion::Now());
        if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    NoteView Visual(Motion::Time now) {
        NoteView v = view_; v.note = EditText(); v.fieldFocused = GetFocus() == edit_ || !settled_;
        v.spinner = SpinnerAngle(now, reduced_); v.reduced = reduced_;
        v.visuals[NoteAdd] = motion_.Visual(NoteAdd, now); if (view_.ask) v.visuals[NoteAsk] = motion_.Visual(NoteAsk, now);
        v.shot = shot_.width ? &shot_ : nullptr;
        return v;
    }
    RECT GhostRect() const { const int m = Px(Spec::popoverLift); return {final_.left - m, final_.top - m, final_.right + m, final_.bottom + m}; }
    void PaintGhost(Motion::Time now) {
        const Pose pose = presence_.Sample(now, reduced_, liftX_, liftY_);
        const float lift = Spec::popoverLift;
        NoteView v = Visual(now); v.drawNote = true; const NoteLayout l = layout_; const D2D1_POINT_2F o = origin_;
        ghost_.paint = [v, l, pose, o, lift](Ink& ink, float, float) {
            auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Translation(lift, lift) * base);
            Posed(ink, pose, o, [&] { PaintNote(ink, v, l); });
            t->SetTransform(base);
        };
        ghost_.Present(GhostRect(), true);
    }
    // ---- the pill (owned no-activate layer): fold + entrance
    RECT PillWindowRect() const {   // room for the unfolded card + the entrance lift
        const int x = final_.left, y = final_.bottom + Px(NoteSpec::pillGap) - Px(Spec::pillLift);
        return {x, y, x + Px(NoteSpec::width), y + Px(PillExpandedH(true) + Spec::pillLift + 4)};   // room for either layout
    }
    D2D1_RECT_F PillLocal(Motion::Time now) { return PillRect(I(), view_.capture, NoteSpec::width / 2, Spec::pillLift, Motion::Clamp(fold_.Value(now)), PillWide(shot_.width ? &shot_ : nullptr)); }
    void PlacePill(Motion::Time now) {
        if (!hwnd_ || (!open_ && !pillIn_.Moving(now) && pillIn_.Value(now) <= 0)) { pill_.Hide(); return; }
        const float in = Motion::Clamp(pillIn_.Value(now)); if (in <= 0) { pill_.Hide(); return; }
        PillView pv; pv.context = view_.context; pv.capture = view_.capture; pv.shot = shot_.width ? &shot_ : nullptr; pv.fold = Motion::Clamp(fold_.Value(now));
        pv.spinner = SpinnerAngle(now, reduced_); pv.focus = view_.focus == NotePill && GetFocus() == hwnd_; pv.keyboard = view_.keyboard; pv.iv = motion_.Visual(NotePill, now);
        const D2D1_RECT_F r = PillLocal(now); const bool reduced = reduced_;
        Pose p = presence_.Sample(now, reduced_); p.opacity = std::min(p.opacity, in); p.scale = 1; p.dx = 0; p.dy = reduced ? 0.f : -(1 - in) * Spec::pillLift;
        pill_.paint = [pv, r, p](Ink& ink, float, float) { Posed(ink, p, {0, 0}, [&] { PaintPill(ink, pv, r); }); };
        pill_.Present(PillWindowRect(), true);
    }
    void SetPill(bool open, bool keyboard) {
        if (!PillExpandable(view_.capture)) open = false;
        if (open == pillOpen_) return; pillOpen_ = open; const auto now = Motion::Now();
        (void)keyboard; fold_.Set(open ? 1.f : 0.f, now, reduced_ ? 0 : (open ? Spec::pillUnfold : Spec::noteOut), Curves::out);
        PlacePill(now); Animator::Get().Request(this);
        Announce(open ? TR(note_pill_shown) : TR(note_pill_hidden));
        if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    // ---- interaction
    void Interact() { const auto now = Motion::Now(); motion_.SetInteraction(pillHover_ ? int(NotePill) : hover_, pillPressed_ ? (pillPressed_ == pillHover_ ? int(NotePill) : 0) : (pressed_ == hover_ ? pressed_ : 0), now); Repaint(); PlacePill(now); Animator::Get().Request(this); }
    void Announce(const std::wstring& text) { if (hooks_.announce) hooks_.announce(text); else uia_.Notify(GroupKey, text.c_str(), L"AnnotationNote"); }
    void Submit() {
        if (view_.status == NoteStatus::Saving) return;
        const std::wstring note = EditText();
        if (note.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
            view_.message = TR(note_need_text); view_.fieldError = true; Resize();
            view_.focus = 0; SetFocus(edit_); Announce(view_.message); return;
        }
        view_.status = NoteStatus::Saving; view_.message.clear(); view_.fieldError = false; Resize();
        NoteSubmission s; s.note = note; s.includeShot = view_.capture != CaptureState::Unavailable; s.site = NoteSite(view_.context); s.projectId = projectId_;
        if (hooks_.submit) hooks_.submit(s);
    }
    void Cancel(bool keyboard) { const std::wstring draft = EditText(); Close(true); if (hooks_.cancel) hooks_.cancel(draft, keyboard); }
    void Activate(int hit, bool keyboard) {
        switch (hit) {
        case NoteField: SetFocus(edit_); break;
        case NoteAdd: Submit(); break;
        case NoteAsk: if (view_.ask && view_.status != NoteStatus::Saving && hooks_.ask) hooks_.ask(EditText()); break;
        case NotePill: SetPill(!pillOpen_, keyboard); break;
        }
    }
    int Main() const { return view_.ask ? int(NoteAsk) : int(NoteAdd); }
    std::vector<int> Order() const { std::vector<int> o{NoteField, NoteAdd}; if (view_.ask) o.push_back(NoteAsk); if (PillExpandable(view_.capture)) o.push_back(NotePill); return o; }
    int Current() const { return GetFocus() == edit_ ? int(NoteField) : view_.focus; }
    void FocusStop(int stop) {
        view_.keyboard = true; motion_.ClearInteractions();
        if (stop == NoteField) { view_.focus = 0; SetFocus(edit_); } else { view_.focus = stop; SetFocus(hwnd_); }
        Repaint(); PlacePill(Motion::Now());
        if (uia_.Listening()) uia_.Raise(stop == NoteField ? 0 : stop, UIA_AutomationFocusChangedEventId);
    }
    void Cycle(bool back) {
        const auto o = Order(); auto it = std::find(o.begin(), o.end(), Current());
        int i = it == o.end() ? 0 : int(it - o.begin()) + (back ? -1 : 1); i = (i + int(o.size())) % int(o.size()); FocusStop(o[size_t(i)]);
    }
    bool Key(WPARAM key, bool fromEdit) {
        const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
        if (key == VK_ESCAPE) { if (pillOpen_) { SetPill(false, true); return true; } Cancel(true); return true; }
        if (key == VK_TAB) { Cycle(shift); return true; }
        if (ctrl && shift && key == 'D' && view_.ask) { Activate(NoteAsk, true); return true; }   // Ctrl+Maj+D: « Demander à l'agent »
        if (key == VK_RETURN) {
            // Entrée = the main action (the filled button): « Demander à l'agent » when the chat is available here,
            // « Ajouter » otherwise. Ctrl+Entrée = « Ajouter » always. Maj+Entrée = a new line. A focused button wins.
            if (ctrl) { Submit(); return true; }
            if (fromEdit) { if (shift) return false; Activate(Main(), true); return true; }
            Activate(view_.focus ? view_.focus : Main(), true); return true;
        }
        if (!fromEdit && key == VK_SPACE && view_.focus) { Activate(view_.focus, true); return true; }
        return false;
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<NotePopover*>(data);
        switch (m) {
        case WM_GETDLGCODE: return DefSubclassProc(h, m, w, l) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN: if (self->Key(w, true)) return 0; break;
        case WM_CHAR: if (w == L'\t' || w == 27) return 0; if (w == L'\r' && GetKeyState(VK_SHIFT) >= 0) return 0; break;
        case WM_SETFOCUS: { const LRESULT r = DefSubclassProc(h, m, w, l); self->view_.focus = 0; self->caret_.Apply(h, FieldGray(), self->CaretColor(), std::max(1, self->Px(1)), self->Px(18)); self->Repaint(); return r; }
        case WM_KILLFOCUS: self->Repaint(); break;
        case WM_LBUTTONDOWN: self->view_.keyboard = false; break;
        case WM_PAINT: {   // placeholder: drawn by GDI under the caret while the field is empty
            const LRESULT r = DefSubclassProc(h, m, w, l);
            if (GetWindowTextLengthW(h) == 0) {
                HDC dc = GetDC(h); RECT c; GetClientRect(h, &c); auto old = SelectObject(dc, self->font_);
                SetTextColor(dc, HintGray()); SetBkMode(dc, TRANSPARENT); DrawTextW(dc, NoteSpec::placeholder(), -1, &c, DT_WORDBREAK | DT_NOPREFIX);
                SelectObject(dc, old); ReleaseDC(h, dc);
            }
            return r; }
        }
        return DefSubclassProc(h, m, w, l);
    }
    COLORREF CaretColor() const { const auto a = Design::Accent::Get(); return NativeUI::AccentCaret::Mix(FieldGray(), RGB(std::lround(a.r * 255), std::lround(a.g * 255), std::lround(a.b * 255)), Design::Accent::caretOpacity); }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<NotePopover*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) { self = static_cast<NotePopover*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    LRESULT Handle(HWND h, UINT m, WPARAM w, LPARAM l) {   // the field's window: the canvas colour under the EDIT, focus and keys
        switch (m) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: { RECT c{}; GetClientRect(h, &c); if (fieldBrush_) FillRect(reinterpret_cast<HDC>(w), &c, fieldBrush_); return 1; }
        case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); if (fieldBrush_) FillRect(dc, &ps.rcPaint, fieldBrush_); EndPaint(h, &ps); return 0; }
        case WM_CTLCOLOREDIT: { HDC dc = reinterpret_cast<HDC>(w); SetTextColor(dc, TextGray()); SetBkColor(dc, FieldGray()); SetBkMode(dc, OPAQUE); return reinterpret_cast<LRESULT>(fieldBrush_); }
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(l) == edit_ && HIWORD(w) == EN_CHANGE) FieldChanged();
            if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) Repaint();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE && open_) { if (hooks_.blurred) hooks_.blurred(); }
            else if (LOWORD(w) != WA_INACTIVE && open_) { if (view_.focus) SetFocus(h); else SetFocus(edit_); }   // back where the user left it
            return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: if (Key(w, false)) return 0; break;
        case WM_SETFOCUS: case WM_KILLFOCUS: Repaint(); PlacePill(Motion::Now()); return 0;
        case WM_APP + 2: view_.keyboard = true; Activate(int(w), true); return 0;
        case WM_DPICHANGED: MakeFont(); Relayout(); PlaceField(); Repaint(); PlacePill(Motion::Now()); return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    // The panel layer's pointer (buttons: hover, press, click). It never takes activation (MA_NOACTIVATE, like the
    // pill): a click on it gives the keyboard back to the field's window when the note had lost it.
    bool PanelHandle(UINT m, WPARAM, LPARAM l, LRESULT& r) {
        if (m != WM_MOUSEMOVE && m != WM_MOUSELEAVE && m != WM_LBUTTONDOWN && m != WM_LBUTTONUP) return false;
        r = 0; if (!open_ || !settled_) return true;
        const float s = 1 / panel_.Scale(); const int hit = m == WM_MOUSELEAVE ? 0 : NoteHitAt(layout_, GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s);
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, panel_.Window(), 0}; TrackMouseEvent(&t); if (hit != hover_) { hover_ = hit; Interact(); } break; }
        case WM_MOUSELEAVE: if (hover_) { hover_ = 0; Interact(); } break;
        case WM_LBUTTONDOWN:
            view_.keyboard = false;
            if (GetForegroundWindow() != hwnd_) SetForegroundWindow(hwnd_);
            if (hit == NoteField) { SetFocus(edit_); break; }
            pressed_ = hit; if (hit) SetCapture(panel_.Window());
            Interact(); break;
        case WM_LBUTTONUP: {
            const int was = pressed_; pressed_ = 0; if (GetCapture() == panel_.Window()) ReleaseCapture(); Interact();
            if (was && was == hit) Activate(hit, false);
            break; }
        }
        return true;
    }
    bool PillHandle(UINT m, WPARAM, LPARAM l, LRESULT& r) {
        const float s = 1 / pill_.Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        const bool in = PillExpandable(view_.capture) && Inside(PillLocal(Motion::Now()), x, y);
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, pill_.Window(), 0}; TrackMouseEvent(&t); const int hv = in ? int(NotePill) : 0; if (hv != pillHover_) { pillHover_ = hv; Interact(); } r = 0; return true; }
        case WM_MOUSELEAVE: pillHover_ = 0; Interact(); r = 0; return true;
        case WM_LBUTTONDOWN: if (in) { pillPressed_ = NotePill; view_.keyboard = false; SetCapture(pill_.Window()); Interact(); } r = 0; return true;
        case WM_LBUTTONUP: { const int was = pillPressed_; pillPressed_ = 0; if (GetCapture() == pill_.Window()) ReleaseCapture(); Interact(); if (was && in) Activate(NotePill, false); r = 0; return true; }
        }
        return false;
    }
    // Typing (EN_CHANGE) or a draft set by Show: clears a validation error and grows the field (then the EDIT scrolls).
    void FieldChanged() {
        if (view_.fieldError) { view_.fieldError = false; view_.message.clear(); }
        HDC dc = GetDC(edit_); auto old = SelectObject(dc, font_); TEXTMETRICW tm{}; GetTextMetricsW(dc, &tm); SelectObject(dc, old); ReleaseDC(edit_, dc);
        const int lines = int(SendMessageW(edit_, EM_GETLINECOUNT, 0, 0)); const float lineDip = (tm.tmHeight + tm.tmExternalLeading) * 96.f / Dpi();
        const float want = std::clamp(lines * lineDip + 4, NoteSpec::fieldMin, NoteSpec::fieldMax);
        if (std::fabs(want - view_.fieldHeight) > .5f) { view_.fieldHeight = want; if (open_) Resize(); } else Repaint();
        if (edit_) InvalidateRect(edit_, nullptr, TRUE);
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
        auto screen = [&](D2D1_RECT_F r) { return RECT{final_.left + Px(r.left), final_.top + Px(r.top), final_.left + Px(r.right), final_.top + Px(r.bottom)}; };   // the panel's place on screen
        Accessibility::Node g; g.key = GroupKey; g.role = Accessibility::Role::Group; g.name = TR(note_title); g.automationId = L"AnnotationNote"; g.focusable = false;
        g.status = view_.status == NoteStatus::Saving ? TR(note_adding_status) : view_.message; g.bounds = final_; n.push_back(g);
        Accessibility::Node site; site.key = SiteKey; site.parent = GroupKey; site.role = Accessibility::Role::Text; site.name = TR(note_site); site.value = NoteSite(view_.context);
        site.focusable = false; site.bounds = screen(layout_.site); site.automationId = L"AnnotationSite"; n.push_back(site);
        Accessibility::Node ok; ok.key = NoteAdd; ok.parent = GroupKey; ok.name = AddLabel(view_.status); ok.accelerator = view_.ask ? TR(key_ctrl_enter) : TR(key_enter); ok.enabled = view_.status != NoteStatus::Saving;
        ok.bounds = screen(layout_.add); ok.automationId = L"AnnotationAdd"; n.push_back(ok);
        if (view_.ask) { Accessibility::Node a; a.key = NoteAsk; a.parent = GroupKey; a.name = TR(chat_note_ask); a.accelerator = TR(key_enter); a.enabled = view_.status != NoteStatus::Saving;
            a.bounds = screen(layout_.ask); a.automationId = L"AnnotationNoteAskAgent"; n.push_back(a); }
        RECT pr{}; if (pill_.Window()) { const auto r = PillLocal(Motion::Now()); pr = pill_.Screen(r); }
        if (PillExpandable(view_.capture)) {
            Accessibility::Node p; p.key = NotePill; p.parent = GroupKey; p.name = NoteSpec::pillLabel(); p.expand = pillOpen_ ? 2 : 1; p.bounds = pr; p.automationId = L"AnnotationContext"; n.push_back(p);
            if (pillOpen_) {
                const std::wstring values[3] = {NotePage(view_.context), NoteElement(view_.context), NoteStyle(view_.context)};
                const wchar_t* labels[3] = {TR(note_ctx_page), TR(note_ctx_element), TR(note_ctx_style)};
                for (int i = 0; i < 3; ++i) { Accessibility::Node r; r.key = PillRowBase + i; r.parent = NotePill; r.role = Accessibility::Role::Text; r.name = labels[i]; r.value = values[i]; r.focusable = false; r.bounds = pr; n.push_back(r); }
                Accessibility::Node c; c.key = ShotKey; c.parent = NotePill; c.role = Accessibility::Role::Text; c.name = TR(note_capture); c.focusable = false; c.bounds = pr;
                c.value = view_.capture == CaptureState::Pending ? TR(note_capture_pending) : TR(note_capture_included); n.push_back(c);
            }
        } else {
            Accessibility::Node p; p.key = ShotKey; p.parent = GroupKey; p.role = Accessibility::Role::Text; p.name = NoteSpec::unavailable(); p.focusable = false; p.bounds = pr; p.automationId = L"AnnotationCaptureUnavailable"; n.push_back(p);
        }
        return n;
    }
    bool Create() {
        if (hwnd_) return true;
        if (!Ink::Shared(root_)) return false;
        // The panel and the ghost: per-pixel-alpha layers (smooth corners; no region, no DWM rounding, no shadow).
        Layer::Options panel; panel.title = TR(note_title);
        Layer::Options ghost; ghost.clickThrough = true; ghost.title = L"";
        if (!panel_.Create(instance_, owner_, panel) || !ghost_.Create(instance_, owner_, ghost)) return false;
        panel_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return PanelHandle(m, w, l, r); };
        fieldBrush_ = CreateSolidBrush(FieldGray());
        // The field's window: a plain rectangle (no region at all), owned by the panel so it stays right above it.
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance_; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserAnnotationNote";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, TR(note_title), WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, panel_.Window(), nullptr, instance_, this);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        NativeUI::ConfigureShadowlessPopup(hwnd_);   // no shadow, no DWM rounding; keepRegion: this window never has a region
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 1, 1, hwnd_, nullptr, instance_, nullptr);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0); SendMessageW(edit_, EM_SETLIMITTEXT, 4000, 0);
        SetWindowSubclass(edit_, EditProc, EditSubclass, reinterpret_cast<DWORD_PTR>(this));
        MakeFont();
        Layer::Options pill; pill.title = NoteSpec::pillLabel();
        if (!pill_.Create(instance_, hwnd_, pill)) return false;
        pill_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return PillHandle(m, w, l, r); };
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? view_.focus : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 2, WPARAM(key), 0); };
        host.expand = [this](long long key, bool) { PostMessageW(hwnd_, WM_APP + 2, WPARAM(key), 0); };
        host.label = [this](HWND child, Accessibility::Node& node) {
            if (child != edit_) return false;
            node.name = NoteSpec::placeholder(); node.automationId = L"AnnotationNoteField"; node.accelerator = view_.ask ? TR(note_keys_ask) : TR(note_keys);
            if (view_.fieldError) node.help = view_.message; return true; };
        uia_.Attach(std::move(host));
        motion_.EnablePolish();
        return true;
    }
    void Settle() {   // V3 (no flash): the panel and its field painted and opaque first, then the ghost goes (overlap, never a gap)
        settled_ = true;
        PresentPanel(true); PlaceField(); InvalidateRect(hwnd_, nullptr, TRUE); UpdateWindow(hwnd_);
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        DwmFlush();
        ghost_.Hide();
    }
    std::wstring projectId_;
public:
    ~NotePopover() { Dispose(); }
    void Dispose() {
        if (Animator::Exists()) Animator::Get().Forget(this);
        uia_.Detach();
        ghost_.Destroy(); pill_.Destroy();
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
        panel_.Destroy();   // after the field's window it owns
        if (font_) DeleteObject(font_);
        if (fieldBrush_) DeleteObject(fieldBrush_);
        font_ = nullptr; fieldBrush_ = nullptr; hwnd_ = edit_ = nullptr; open_ = false;
    }
    void Init(HINSTANCE instance, HWND owner, std::wstring root, NoteHooks hooks) { instance_ = instance; owner_ = owner; root_ = std::move(root); hooks_ = std::move(hooks); }
    bool Created() const { return hwnd_ != nullptr; }
    bool Visible() const { return open_; }
    // The window's accent changed: « Ajouter », the focused border, the checkboxes, the spinner and the caret repaint
    // in the accent as presented (no clock of its own: called once per step of the profile fade).
    void AccentChanged() {
        if (!hwnd_ || !open_) return;
        if (GetFocus() == edit_) caret_.Apply(edit_, FieldGray(), CaretColor(), std::max(1, Px(1)), Px(18));
        Repaint(); PlacePill(Motion::Now());
    }
    HWND Window() const { return hwnd_; }          // the field's window: where the keyboard focus lives
    HWND PanelWindow() const { return panel_.Window(); }
    HWND Edit() const { return edit_; }
    HWND PillWindow() const { return pill_.Window(); }
    // target: the element rect, bounds: the page area (both screen px).
    bool Show(const NoteRequest& request, RECT target, RECT bounds, bool keyboard, bool reduced) {
        if (!Create()) return false;
        const auto now = Motion::Now();
        projectId_ = request.projectId;
        view_ = NoteView{}; view_.context = request.context; view_.capture = request.capture; view_.keyboard = keyboard; view_.ask = request.askAgent;
        if (request.capture != CaptureState::Ready) shot_ = Shot{};
        reduced_ = reduced; target_ = target; bounds_ = bounds; hover_ = pressed_ = pillHover_ = pillPressed_ = 0; motion_.ClearInteractions(); motion_.SetReducedMotion(reduced);
        open_ = false; SetWindowTextW(edit_, request.draft.c_str()); SendMessageW(edit_, EM_SETSEL, WPARAM(request.draft.size()), LPARAM(request.draft.size()));
        Relayout(); PlaceField();   // the field's width first: the draft's line count depends on it
        FieldChanged(); view_.fieldError = false; view_.message.clear();
        Relayout(); Place(); PlaceField();
        panel_.Hide();              // the ghost plays the entrance; the panel appears, complete, when it settles
        open_ = true; settled_ = false;
        Timing t = NoteTiming(); presence_.Configure(t); presence_.Show(now, true, reduced);
        pillOpen_ = request.expanded && PillExpandable(request.capture); fold_.Snap(pillOpen_ ? 1.f : 0.f);
        pillIn_.Snap(0); pillIn_.Set(1, now, reduced ? Spec::modalReducedIn : Spec::pillUnfold, Curves::out, reduced ? 0 : Spec::pillDelay);
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        ShowWindow(hwnd_, SW_SHOW); EnableWindow(hwnd_, TRUE);
        SetForegroundWindow(hwnd_); SetFocus(edit_);
        PaintGhost(now);            // reduced motion: the same ghost, opacity only (Presence::Sample)
        PlacePill(now);
        Animator::Get().Request(this);
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); uia_.Raise(GroupKey, UIA_Window_WindowOpenedEventId); }
        return true;
    }
    void SetShot(CaptureState state, const Shot* shot) {
        view_.capture = state; if (shot && state == CaptureState::Ready) shot_ = *shot; else if (state != CaptureState::Ready) shot_ = Shot{};
        if (state == CaptureState::Unavailable && pillOpen_) { pillOpen_ = false; fold_.Snap(0); }
        if (!hwnd_) return;
        Repaint(); PlacePill(Motion::Now()); Animator::Get().Request(this);
        if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    void SetProjects(std::vector<Project>, const std::wstring& selected) { projectId_ = selected; }   // V1, no selector in V2
    // The write finished: the panel folds toward the launcher (bottom-right) in 150 ms.
    void Saved() { view_.status = NoteStatus::Editing; origin_ = {NoteSpec::width, layout_.height}; liftX_ = liftY_ = Spec::popoverLift; Close(true); }
    // The write failed: keep the note, say so, offer « Réessayer » (focus on it).
    void Failed(const std::wstring& message = TR(note_save_failed)) {
        if (!hwnd_) return; view_.status = NoteStatus::Failed; view_.message = message; Resize();
        view_.focus = NoteAdd; SetFocus(hwnd_); Repaint(); Announce(message);
    }
    void Close(bool animate = true) {
        if (!open_ || !hwnd_) return;
        const auto now = Motion::Now(); open_ = false;
        const bool ghost = animate && presence_.Progress(now) > 0;
        presence_.Hide(now, animate, reduced_);
        pillIn_.Set(0, now, animate ? (reduced_ ? Spec::modalReducedOut : Spec::noteOut) : 0, Curves::out);
        if (GetForegroundWindow() == hwnd_ && owner_) SetForegroundWindow(owner_);
        if (ghost) { PaintGhost(now); DwmFlush(); }   // V3: the ghost is on screen before the panel goes
        else ghost_.Hide();
        ShowWindow(hwnd_, SW_HIDE); panel_.Hide();
        PlacePill(now);
        if (uia_.Listening()) uia_.Raise(GroupKey, UIA_Window_WindowClosedEventId);
        if (presence_.Moving(now) || pillIn_.Moving(now)) Animator::Get().Request(this);
    }
    bool Frame(Motion::Time now) override {
        if (!hwnd_) return false;
        const bool moving = presence_.Moving(now);
        if (moving) PaintGhost(now);
        else if (!open_) ghost_.Hide();
        else if (!settled_) Settle();
        const bool pillMoving = pillIn_.Moving(now) || fold_.Moving(now);
        const bool spin = open_ && pillOpen_ && view_.capture == CaptureState::Pending && !reduced_;
        if (pillMoving || spin || moving || motion_.NextWake(now).delay) PlacePill(now);
        if (open_ && settled_ && motion_.NextWake(now).delay) Repaint();
        return moving || pillMoving || spin || motion_.NextWake(now).delay.has_value();
    }
    // ---- tests / QA
    const NoteView& ViewForTest() const { return view_; }
    const NoteLayout& LayoutForTest() const { return layout_; }
    Presence& PresenceForTest() { return presence_; }
    Layer& GhostForTest() { return ghost_; }
    Layer& PillForTest() { return pill_; }
    Layer& PanelForTest() { return panel_; }
    bool SettledForTest() const { return settled_; }
    bool PillOpenForTest() const { return pillOpen_; }
    bool KeyForTest(WPARAM key, bool fromEdit) { return Key(key, fromEdit); }
    void TypeForTest(const std::wstring& text) { SetWindowTextW(edit_, text.c_str()); FieldChanged(); }
    void ActivateForTest(int hit) { Activate(hit, true); }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
    RECT FinalForTest() const { return final_; }
    D2D1_POINT_2F OriginForTest() const { return origin_; }
    float LiftXForTest() const { return liftX_; }
    int FocusForTest() const { return Current(); }
};
}} // namespace NativeChrome::Annotations
