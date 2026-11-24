#pragma once
#include "i18n.h"
// Annotation mode onboarding (#125, variant A) — the window. Paper « Browser Design » › « MCP - Design UI UX », row 1
// under « #125 · Onboarding du mode Annotation — 3 propositions »: A1 1MLX-0 « À quoi ça sert », A2 1MNM-0 « Où ça
// marche », A3 1MPB-0 « Connecter », A4 1MR0-0 « Prêt ». Flow: src/annotation-onboarding-flow.h. Host: src/annotation-mode.h.
// Values with their nodes: design/annotation-onboarding-values.md.
//
// The first-run onboarding's window family (src/onboarding.h board 6, drawn by the same code: src/onboarding-parts.h):
//   720 × 580, radius 12, #000000, no border. GrainGradient 725 × 407 at (−2, −2) over #DEFDFF + board 6's dither; the
//   veil y 249 … 579 (black 0 % at 0.3 % → black at 47.2 %). Header 48: « Français ⌄ » at (12, 10), close 28 at (680, 10),
//   #474747. Text block, bottom at y 498: title 32/38 500 −0.035 em white, 5, body 14/20 #B3B3B3 (one or two lines),
//   centred. Footer 66 high at y 514: secondary raised #292929 at x 19, primary #0099A0 right-aligned at 701, both h 32,
//   radius 6, padding-inline 12, 14/20 500 white, y 531. Progress (1MSP-0): 4 pills h 6 radius 3, gap 6, centred on
//   (360, 547): the current one 16 wide #FFFFFF, the others 6 wide #FFFFFF40.
//   The boards are drawn for the sample profile (200° / 88 %): their turquoise is kept as is (the Paper colours).
//
//   A1 (1MLY-0 at (81, 155)) board 6's illustration: « Payer 48,00 € », its frame and tags, the crosshair, the note.
//   A2 (1MTD-0 at (120, 104), 480 wide, column gap 14):
//     rows 48 high, radius 12, padding 16 / 8, gap 10: URL 16/20 400 −0.01 em (« localhost » white + « :5173 » #FFFFFF80;
//     « lemonde.fr » #FFFFFF99 + « /planete » #FFFFFF4D, the row #171717E6); button h 32 radius 8 padding 10 gap 6:
//     crosshair 14 (stroke 3) + « Annoter » 13/16 500 — #0099A0 / white, or #FFFFFF0F / #FFFFFF52.
//     Tooltip at the right: #171717, border 1 #FFFFFF14, radius 8, padding 6 / 10, gap 2, max 280: 13/18 #FFFFFFE6
//     « Seulement sur vos projets locaux » + 13/18 #FFFFFF80 « Lancez votre projet, puis ouvrez localhost. »
//   A3 (1MTU-0 at (180, 112), 360 wide): #171717, radius 16, padding 12, gap 8. One row per app, h 40, radius 8,
//     14/18 500: connected = border 1 #FFFFFF14, logo 16 + « Claude est connecté » + check 14 #2FD1D9 (gap 6); to connect
//     = raised #292929, « Connecter avec Codex » + logo 16. Then « Autre app ou réglage manuel » 12/16 #FFFFFF80,
//     padding-top 4. States Paper does not draw (en cours, échec, reconnecter, the restart note) take the « Connecter
//     un agent » modal's (src/annotation-connect.h) at this row's size.
//   A4 (1MUD-0 at y 120, centred, gap 20): keys h 56, radius 12, #171717, border-bottom 3 #0B0B0B, padding-inline 18,
//     20/24 500 white (« A » 56 wide), « + » 18/24 500 #0B3B3D, gap 10; status pill h 32 radius 16 #171717 padding 12
//     gap 8: dot 8 #2FD1D9 + « Claude est connecté » 13/16 500 #FFFFFFE6.
//
// Window: an owned popup centred over the browser's client area, over the « Connecter un agent » modal's veil
// (#0000008C); the browser waits behind it (disabled) as for that modal. Motion (the first-run onboarding's):
// in 150 ms (veil 200 ms), screen changes 220 ms (the old content fades out, the new one fades in; the gradient and
// the header stay), out 120 ms (180 at the end); reduced motion: 120 ms changes, still shaders. Hover 120 ms.
// Keyboard: Tab / Maj+Tab through A3's rows, the footer's buttons, the language and close buttons; Entrée = the
// focused control, else the main button; Espace = the focused control; Échap = « Plus tard ».
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "chrome.h"
#include "tooltip.h"
#include "accessibility.h"
#include "context-menu.h"
#include "frame-clock.h"
#include "native-ui.h"
#include "ui-language.h"
#include "onboarding-flow.h"
#include "onboarding-parts.h"
#include "annotation-onboarding-flow.h"
#include "annotation-connect.h"

namespace NativeChrome {
namespace AnnotationOnboardingUI {

namespace K = OnboardingKit; namespace P = OnboardingParts; namespace AO = AnnotationOnboarding; namespace A = Annotations;
using AO::Step;

namespace Layout {
inline constexpr float width = 720, height = 580, radius = 12;
inline constexpr float headerTop = 10, langLeft = 12, closeLeft = 680;                    // 1MNB-0: padding 10 / 12
inline constexpr float barTop = 531, barLeft = 19, barRight = 701;                       // 1MMZ-0: 66 high at y 514, padding-inline 19
inline constexpr float textBottom = 498, titleH = 38, titleGap = 5, line = 20;          // 1MN8-0, 16 above the footer
inline constexpr float dotsCentre = 547, dot = 6, dotWide = 16, dotGap = 6;             // 1MSP-0
// A2
inline constexpr float rowsLeft = 120, rowsTop = 104, rowsW = 480, rowH = 48, rowsGap = 14, rowRadius = 12, rowPadL = 16, rowPadR = 8;
inline constexpr float pickH = 32, pickPad = 10, pickRadius = 8, tipMax = 280;
// A3
inline constexpr float cardLeft = 180, cardTop = 112, cardW = 360, cardPad = 12, cardGap = 8, cardRadius = 16, appH = 40, appRadius = 8;
// A4
inline constexpr float keysTop = 120, keyH = 56, keyPad = 18, keyGap = 10, keyRadius = 12, keyBorder = 3, keyA = 56, pillGap = 20, pillH = 32, pillPad = 12;
}
inline constexpr unsigned Turquoise = 0x0099A0, Cyan = 0x2FD1D9;

struct Hooks {
    std::function<void(A::AgentKind)> connect;                     // A3: « Connecter avec Claude / Codex » (explicit click); the host answers with SetStatus
    std::function<void(AO::Ending, bool manual)> closed;           // after the window left; manual: « Autre app ou réglage manuel » was the way out
    std::function<void(I18n::Locale)> language;                    // the header's language menu: persist the choice
    std::function<void(const std::wstring&)> announce;             // screen reader notifications (host: the browser's)
};
struct Options {
    bool reduced = false;
    A::AppStatus claude, codex;    // what A3 / A4 show first; then SetStatus
    Hooks hooks;
};

class Window {
public:
    enum Id : int { None = 0, Lang = 1, Close = 2, Primary = 10, Secondary = 11, ClaudeRow = 20, CodexRow = 21, Manual = 22 };
    static constexpr UINT FrameMessage = WM_APP + 0x4E0;
private:
    struct Item { int id; D2D1_RECT_F r; };
    HINSTANCE instance_ = nullptr; HWND hwnd_ = nullptr, veil_ = nullptr, owner_ = nullptr; std::wstring root_;
    Chrome chrome_; Chrome* c_ = &chrome_; Accessibility::Bridge uia_; ContextMenu::Menu menu_; FrameClock::Clock clock_; bool clockOk_ = false;
    K::Glyphs glyphs_; Onboarding::Shaders::Renderer gpu_; bool gpuTried_ = false; Onboarding::Shaders::Layer grain_; P::TrameLayer trame_;
    AO::Flow flow_; Options options_; A::AppStatus claude_, codex_; bool manual_ = false, silent_ = false;
    std::vector<Item> items_; Step arranged_ = Step::What;
    UINT dpi_ = 96; bool offscreen_ = false, keyboard_ = false, menuOpen_ = false, open_ = false;
    int focus_ = None, hover_ = None, pressed_ = None;
    std::map<int, float> glow_;
    Motion::Time opened_{}, lastStep_{}, changed_{}, left_{};
    Step shown_ = Step::What, from_ = Step::What; bool changing_ = false, entering_ = false, leaving_ = false;
    double secondsForTest_ = -1;

    // ---- small helpers ----
    float Scale() const { return dpi_ / 96.f; }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Scale())); }
    static float Ms(Motion::Time from, Motion::Time to) { return static_cast<float>(std::chrono::duration<double, std::milli>(to - from).count()); }
    static float EaseOut(float u) { u = Motion::Clamp(u); const float v = 1 - u; return 1 - v * v * v * v; }
    static bool In(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
    static D2D1_RECT_F Box(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    // The boards' sample profile: their colours as Paper draws them.
    static const Profiles::Profile& Seed() { static const Profiles::Profile p = [] { Profiles::Profile s; s.hue = K::PaperHue; s.intensity = K::PaperChroma; s.neutral = false; return s; }(); return p; }
    static AO::Link LinkOf(const A::AppStatus& s) {
        switch (s.state) { case A::ConnectState::Working: return AO::Link::Working; case A::ConnectState::Connected: return AO::Link::Connected; case A::ConnectState::Failed: return AO::Link::Failed; case A::ConnectState::Reconnect: return AO::Link::Reconnect; default: return AO::Link::Idle; }
    }
    const A::AppStatus& App(A::AgentKind a) const { return a == A::AgentKind::Claude ? claude_ : codex_; }
    AO::Reader Reader() const { return AO::ReaderOf(LinkOf(claude_), LinkOf(codex_)); }
    void Announce(const std::wstring& text) { if (text.empty()) return; if (options_.hooks.announce) options_.hooks.announce(text); else if (uia_.Attached()) uia_.Notify(500, text.c_str(), L"AnnotationOnboarding"); }   // i18n-exempt: UIA activity id

    // ---- texts of each board ----
    std::wstring Title(Step s) const {
        switch (s) { case Step::Where: return TR(annotation_onboarding_where_title); case Step::Connect: return TR(annotation_onboarding_connect_title); case Step::Ready: return TR(annotation_onboarding_ready_title); default: return TR(annotation_onboarding_what_title); }
    }
    std::wstring Body(Step s) const {
        switch (s) {
        case Step::Where: return TR(annotation_onboarding_where_body);
        case Step::Connect: return TR(annotation_onboarding_connect_body);
        case Step::Ready: { const auto r = Reader(); return TRF(annotation_onboarding_ready_body, r == AO::Reader::None ? std::wstring(TR(annotation_onboarding_your_agent)) : std::wstring(AO::ReaderName(r))); }
        default: return TR(annotation_onboarding_what_body);
        }
    }
    static int Lines(const std::wstring& text) { return 1 + static_cast<int>(std::count(text.begin(), text.end(), L'\n')); }
    std::wstring SecondaryLabel(Step s) const { return s == Step::What ? TR(common_later) : TR(common_back); }
    std::wstring PrimaryLabel(Step s) const { return s == Step::Connect ? TR(onboarding_continue) : s == Step::Ready ? TR(annotation_onboarding_start) : TR(annotation_onboarding_next); }
    std::wstring AppLabel(A::AgentKind a) const { return A::AppButtonLabel(a, App(a)); }
    // The message under an app's row (the modal's): a failure, or a note once connected (« Redémarrez Claude pour l'activer »).
    std::wstring AppMessage(A::AgentKind a) const { const auto& s = App(a); return s.state == A::ConnectState::Failed || s.state == A::ConnectState::Connected ? s.detail : std::wstring(); }

    // ---- geometry of the interactive parts ----
    D2D1_RECT_F LangBox() { return Box(Layout::langLeft, Layout::headerTop, P::LanguageWidth(*c_), P::HeaderSize); }
    static D2D1_RECT_F CloseBox() { return Box(Layout::closeLeft, Layout::headerTop, P::HeaderSize, P::HeaderSize); }
    D2D1_RECT_F SecondaryBox(Step s) { return Box(Layout::barLeft, Layout::barTop, P::ButtonWidth(*c_, SecondaryLabel(s), 500), P::ButtonHeight); }
    D2D1_RECT_F PrimaryBox(Step s) { const float w = P::ButtonWidth(*c_, PrimaryLabel(s), 500); return Box(Layout::barRight - w, Layout::barTop, w, P::ButtonHeight); }
    struct Card { D2D1_RECT_F card, claude, codex, claudeMsg, codexMsg, manual; };
    Card CardLayout() {
        Card k{}; const float inner = Layout::cardLeft + Layout::cardPad, w = Layout::cardW - 2 * Layout::cardPad; float y = Layout::cardTop + Layout::cardPad;
        for (A::AgentKind a : {A::AgentKind::Claude, A::AgentKind::Codex}) {
            const D2D1_RECT_F row = Box(inner, y, w, Layout::appH); y += Layout::appH + Layout::cardGap;
            D2D1_RECT_F msg{};
            if (!AppMessage(a).empty()) { msg = Box(inner, y, w, 16); y += 16 + Layout::cardGap; }
            (a == A::AgentKind::Claude ? k.claude : k.codex) = row; (a == A::AgentKind::Claude ? k.claudeMsg : k.codexMsg) = msg;
        }
        const K::Font f{12, 16, 400, 0}; const float tw = K::Width(*c_, TR(settings_manual_connect), f);
        y += 4; k.manual = Box(Layout::width / 2 - tw / 2, y, tw, 16); y += 16 + Layout::cardPad;
        k.card = {Layout::cardLeft, Layout::cardTop, Layout::cardLeft + Layout::cardW, y};
        return k;
    }
    void Arrange() { Arrange(flow_.step()); }
    void Arrange(Step s) {
        items_.clear(); arranged_ = s;
        if (s == Step::Closed) return;
        if (s == Step::Connect) { const auto k = CardLayout(); items_.push_back({ClaudeRow, k.claude}); items_.push_back({CodexRow, k.codex}); items_.push_back({Manual, {k.manual.left - 6, k.manual.top - 4, k.manual.right + 6, k.manual.bottom + 4}}); }
        items_.push_back({Secondary, SecondaryBox(s)}); items_.push_back({Primary, PrimaryBox(s)});
        items_.push_back({Lang, LangBox()}); items_.push_back({Close, CloseBox()});
    }
    D2D1_RECT_F Rect(int id) const { for (const auto& i : items_) if (i.id == id) return i.r; return {}; }
    bool Enabled(int id) const {
        if (id == ClaudeRow) return AO::Connectable(LinkOf(claude_));
        if (id == CodexRow) return AO::Connectable(LinkOf(codex_));
        return true;
    }
    int Hit(float x, float y) const { if (leaving_) return None; for (auto it = items_.rbegin(); it != items_.rend(); ++it) if (In(it->r, x, y)) return Enabled(it->id) ? it->id : None; return None; }
    std::vector<int> TabOrder() const {
        std::vector<int> o; if (flow_.step() == Step::Closed) return o;
        if (flow_.step() == Step::Connect) { if (Enabled(ClaudeRow)) o.push_back(ClaudeRow); if (Enabled(CodexRow)) o.push_back(CodexRow); o.push_back(Manual); }
        o.push_back(Secondary); o.push_back(Primary); o.push_back(Lang); o.push_back(Close);
        return o;
    }

    // ---- time ----
    void RequestFrame() { if (!hwnd_ || offscreen_) return; if (clockOk_) clock_.Request(); else SetTimer(hwnd_, 1, 16, nullptr); }
    bool Shown() const { return hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_); }
    bool ShadersLoop() const { return !offscreen_ && !leaving_ && Onboarding::ShaderLoops(options_.reduced, gpu_.Animated(), Shown(), owner_ && IsIconic(owner_)); }
    double ShaderSeconds(Motion::Time now) const {
        if (secondsForTest_ >= 0) return secondsForTest_;
        if (offscreen_ || options_.reduced || !gpu_.Animated()) return 0;
        return std::chrono::duration<double>(now - opened_).count();
    }
    float Change(Motion::Time now) const { return changing_ ? Motion::Clamp(Ms(changed_, now) / static_cast<float>(Onboarding::TransitionMs(options_.reduced))) : 1.f; }
    bool Spinning() const { return !options_.reduced && (flow_.step() == Step::Connect || shown_ == Step::Connect) && (claude_.state == A::ConnectState::Working || codex_.state == A::ConnectState::Working); }
    bool Animating() const {
        if (changing_ || entering_ || leaving_ || Spinning()) return true;
        for (const auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f; if (std::fabs(g.second - want) > .001f) return true; }
        return false;
    }
    bool Due(Motion::Time now) const { return grain_.shaded == Motion::Time{} || Ms(grain_.shaded, now) >= 1000.f / 30 - 3; }
    float VeilIn(Motion::Time now) const { return Motion::Clamp(Ms(opened_, now) / static_cast<float>(options_.reduced ? A::Spec::modalReducedIn : A::Spec::modalIn)); }
    float LeaveMs() const { return flow_.ending() == AO::Ending::Finished ? static_cast<float>(Onboarding::OutroFadeMs) : 120.f; }
    void SetVeil(float u) { if (veil_) SetLayeredWindowAttributes(veil_, 0, static_cast<BYTE>(std::lround(255 * A::Spec::veilOpacity * Motion::Clamp(u))), LWA_ALPHA); }
    void Step_() {
        const auto now = Motion::Now(); const float dt = lastStep_ == Motion::Time{} ? 16 : std::min(100.f, Ms(lastStep_, now)); lastStep_ = now;
        if (entering_) { const float u = Motion::Clamp(Ms(opened_, now) / 150.f); PopupKit::SetAlpha(hwnd_, u); SetVeil(VeilIn(now)); if (u >= 1 && VeilIn(now) >= 1) entering_ = false; }
        if (leaving_) {
            const float u = Motion::Clamp(Ms(left_, now) / LeaveMs());
            PopupKit::SetAlpha(hwnd_, 1 - u); SetVeil(1 - u);
            if (u >= 1) { leaving_ = false; Finish(); return; }
        }
        if (changing_ && Change(now) >= 1) { changing_ = false; from_ = shown_; Invalidate(); }
        for (auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f, step = dt / 120.f; g.second = want > g.second ? std::min(want, g.second + step) : std::max(want, g.second - step); }
        if (Animating() || (ShadersLoop() && Due(now))) Invalidate();
        if (Animating() || ShadersLoop()) RequestFrame(); else if (!clockOk_ && hwnd_) KillTimer(hwnd_, 1);
    }

    // ---- flow events ----
    void Changed(Step before) {
        const Step now = flow_.step();
        if (now == before) { Arrange(); Invalidate(); return; }
        if (now == Step::Closed) { Leave(); return; }
        from_ = shown_; shown_ = now; changed_ = Motion::Now(); changing_ = !offscreen_;
        focus_ = keyboard_ ? Primary : None; hover_ = None; pressed_ = None;
        Arrange(); Invalidate(); RequestFrame();
        if (uia_.Attached()) uia_.ChildrenChanged(0);
        Announce(Title(now) + L". " + Body(now));
    }
    void Leave() { if (leaving_ || !open_) return; leaving_ = true; left_ = Motion::Now(); if (offscreen_ || !hwnd_) { leaving_ = false; Finish(); return; } RequestFrame(); }
    // The window is gone: the browser is given back, then the host hears how it ended.
    void Finish() {
        if (!open_) return; open_ = false;
        // The browser first: hidden while it is still disabled, the window would hand the activation to another app.
        if (owner_ && !offscreen_) { EnableWindow(owner_, TRUE); SetForegroundWindow(owner_); }
        if (hwnd_) { clock_.Stop(); KillTimer(hwnd_, 1); ShowWindow(hwnd_, SW_HIDE); }
        if (veil_) ShowWindow(veil_, SW_HIDE);
        Dispose();
        const AO::Ending ending = flow_.ending(); const bool manual = manual_, silent = silent_;
        if (!silent && options_.hooks.closed) options_.hooks.closed(ending, manual);
    }
    void Dispose() {
        if (uia_.Attached()) uia_.Detach();
        menu_.Dispose(); grain_.Drop(); trame_.Drop(); gpu_.Dispose(); gpuTried_ = false; glyphs_.Clear();
        if (hwnd_) { chrome_.Dispose(); DestroyWindow(hwnd_); hwnd_ = nullptr; }
        if (veil_) { DestroyWindow(veil_); veil_ = nullptr; }
    }
    void SetLanguage(I18n::Locale l) {
        if (l == I18n::Current()) return;
        I18n::SetForProcess(l);
        if (options_.hooks.language) options_.hooks.language(l);
        if (hwnd_) SetWindowTextW(hwnd_, Title(flow_.step()).c_str());
        Arrange(); Invalidate();
        if (owner_ && !offscreen_) InvalidateRect(owner_, nullptr, FALSE);
        if (uia_.Attached()) uia_.ChildrenChanged(0);
    }
    void LanguageMenu(bool keyboard) {
        if (!hwnd_ || offscreen_) return;
        std::vector<ContextMenu::Item> items;
        for (I18n::Locale l : {I18n::Locale::Fr, I18n::Locale::En}) { ContextMenu::Item i; i.id = 1 + static_cast<int>(l); i.label = UiLanguage::Autonym(l); i.checked = l == I18n::Current(); items.push_back(i); }
        const auto r = Rect(Lang); POINT at{Px(r.left), Px(r.bottom + 4)}; ClientToScreen(hwnd_, &at);
        menuOpen_ = true; Invalidate();
        const int chosen = menu_.Track(items, at, nullptr, keyboard, options_.reduced, false, true);
        menuOpen_ = false; Invalidate();
        if (chosen) SetLanguage(chosen == 2 ? I18n::Locale::En : I18n::Locale::Fr);
    }
    void Activate(int id, bool keyboard) {
        if (!Enabled(id) || leaving_ || !flow_.open()) return;
        const Step before = flow_.step();
        switch (id) {
        case Lang: LanguageMenu(keyboard); return;
        case Close: flow_.Skip(); break;
        case Secondary: if (before == Step::What) flow_.Skip(); else flow_.Back(); break;
        case Primary: flow_.Next(); break;
        case ClaudeRow: case CodexRow: if (before == Step::Connect && options_.hooks.connect) options_.hooks.connect(id == ClaudeRow ? A::AgentKind::Claude : A::AgentKind::Codex); return;
        case Manual: if (before != Step::Connect) return; manual_ = true; flow_.Skip(); break;
        default: return;
        }
        Changed(before);
    }
    void Focus(int id, bool keyboard) { keyboard_ = keyboard; focus_ = id; if (hwnd_ && GetFocus() != hwnd_ && !offscreen_) SetFocus(hwnd_); Invalidate(); if (uia_.Attached()) uia_.Raise(id, UIA_AutomationFocusChangedEventId); }
    void TabTo(bool back) {
        const auto order = TabOrder(); if (order.empty()) return;
        auto it = std::find(order.begin(), order.end(), focus_); int next;
        if (it == order.end()) next = back ? order.back() : order.front();
        else if (back) next = it == order.begin() ? order.back() : *(it - 1);
        else next = it + 1 == order.end() ? order.front() : *(it + 1);
        Focus(next, true);
    }
    bool Key(WPARAM key) {
        keyboard_ = true; const bool shift = GetKeyState(VK_SHIFT) < 0;
        if (key == VK_ESCAPE) { Activate(Close, true); return true; }
        if (key == VK_TAB) { TabTo(shift); return true; }
        if (key == VK_RETURN) { if (focus_ == None) Activate(Primary, true); else Activate(focus_, true); return true; }
        if (key == VK_SPACE && focus_ != None) { Activate(focus_, true); return true; }
        if ((key == VK_DOWN || key == VK_F4) && focus_ == Lang) { Activate(Lang, true); return true; }
        return false;
    }

    // ---- shaders ----
    void EnsureGpu() {
        if (gpuTried_) return; gpuTried_ = true;
        namespace S = Onboarding::Shaders;
        if (!gpu_.Init()) return;
        S::Image noise; if (S::LoadPng(c_->Imaging(), root_ + L"\\assets\\onboarding\\noise.png", noise)) gpu_.SetInput(S::Kind::GrainGradient, noise);
    }

    // ---- drawing ----
    // One line in two colours (A2's addresses): one layout, its tail recoloured, so the second part starts at the
    // first one's exact advance (kerning included).
    void TwoTone(const std::wstring& head, const std::wstring& tail, D2D1_RECT_F box, K::Font f, D2D1_COLOR_F a, D2D1_COLOR_F b) {
        auto* t = c_->RenderTarget(); auto* brush = c_->SolidBrush(); if (!t || !brush) return;
        auto* layout = K::MakeLayout(*c_, head + tail, box.right - box.left, f, false, DWRITE_TEXT_ALIGNMENT_LEADING, true); if (!layout) return;
        ID2D1SolidColorBrush* second = nullptr;
        if (SUCCEEDED(t->CreateSolidColorBrush(b, &second)) && second) layout->SetDrawingEffect(second, {static_cast<UINT32>(head.size()), static_cast<UINT32>(tail.size())});
        brush->SetColor(a); t->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        if (second) second->Release();
        layout->Release();
    }
    void DrawButtons(Step s, float alpha) {
        auto button = [&](int id, const std::wstring& label, bool accent) {
            const auto r = Rect(id); if (r.right <= r.left) return;
            const float glow = glow_[id], press = pressed_ == id && hover_ == id ? 1.f : 0.f;
            P::FooterButton(*c_, r, label, accent ? Turquoise : K::Token::raised, accent, 500, glow, press, true, focus_ == id && keyboard_, alpha);
        };
        button(Secondary, SecondaryLabel(s), false); button(Primary, PrimaryLabel(s), true);
    }
    void DrawDots(Step s, float alpha) {
        const int current = AO::Index(s); const float total = Layout::dotWide + (AO::StepCount - 1) * (Layout::dot + Layout::dotGap); float x = Layout::width / 2 - total / 2;
        const float top = Layout::dotsCentre - Layout::dot / 2;
        for (int i = 0; i < AO::StepCount; ++i) {
            const float w = i == current ? Layout::dotWide : Layout::dot;
            K::Fill(*c_, Box(x, top, w, Layout::dot), 3, i == current ? K::Col(K::Token::text, alpha) : PopupKit::White((0x40 / 255.f) * alpha));
            x += w + Layout::dotGap;
        }
    }
    void DrawText(Step s, float alpha) {
        const std::wstring body = Body(s); const int lines = Lines(body);
        const float bodyTop = Layout::textBottom - lines * Layout::line, titleTop = bodyTop - Layout::titleGap - Layout::titleH;
        K::Text(*c_, Title(s), {0, titleTop, Layout::width, titleTop + Layout::titleH}, {32, 38, 500, -1.12f}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        K::Text(*c_, body, {0, bodyTop, Layout::width, Layout::textBottom}, {14, 20, 400, 0}, K::Col(K::Token::secondary, alpha), DWRITE_TEXT_ALIGNMENT_CENTER, true);
    }
    // A2: a local project, the mode's button on; a site on the web, the button off and why.
    void DrawWhere(float alpha) {
        const K::Font url{16, 20, 400, -.16f}, pick{13, 16, 500, 0}, tip{13, 18, 400, 0};
        const std::wstring annotate = TR(a11y_annotate); const float pw = Layout::pickPad + 14 + 6 + K::Width(*c_, annotate, pick) + Layout::pickPad;
        auto row = [&](float top, unsigned fill, float fillAlpha, const std::wstring& host, const std::wstring& path, D2D1_COLOR_F hostColor, D2D1_COLOR_F pathColor, bool on) {
            const D2D1_RECT_F r = Box(Layout::rowsLeft, top, Layout::rowsW, Layout::rowH);
            K::Fill(*c_, r, Layout::rowRadius, K::Col(fill, fillAlpha * alpha));
            const D2D1_RECT_F b = Box(r.right - Layout::rowPadR - pw, top + (Layout::rowH - Layout::pickH) / 2, pw, Layout::pickH);
            TwoTone(host, path, {r.left + Layout::rowPadL, top + 14, b.left - 10, top + 34}, url, hostColor, pathColor);
            K::Fill(*c_, b, Layout::pickRadius, on ? K::Col(Turquoise, alpha) : PopupKit::White((0x0F / 255.f) * alpha));
            const D2D1_COLOR_F ink = on ? K::Col(K::Token::text, alpha) : PopupKit::White((0x52 / 255.f) * alpha);
            glyphs_.Stroke(*c_, K::Path::cross, Box(b.left + Layout::pickPad, b.top + 9, 14, 14), 32, 3, ink);
            K::Text(*c_, annotate, {b.left + Layout::pickPad + 20, b.top + 8, b.right, b.top + 24}, pick, ink);
        };
        row(Layout::rowsTop, K::Token::canvas, 1, L"localhost", L":5173", K::Col(K::Token::text, alpha), PopupKit::White(.5f * alpha), true);                      // i18n-exempt: an address in the illustration
        row(Layout::rowsTop + Layout::rowH + Layout::rowsGap, K::Token::canvas, 0xE6 / 255.f, L"lemonde.fr", L"/planete", PopupKit::White(.6f * alpha), PopupKit::White((0x4D / 255.f) * alpha), false);   // i18n-exempt: an address in the illustration
        const std::wstring t1 = TR(annotation_onboarding_local_only), t2 = TR(annotation_onboarding_local_hint);
        const float w = std::min(Layout::tipMax, std::max(K::Width(*c_, t1, tip), K::Width(*c_, t2, tip)) + 2 * 10 + 2), top = Layout::rowsTop + 2 * (Layout::rowH + Layout::rowsGap);
        const D2D1_RECT_F r = Box(Layout::rowsLeft + Layout::rowsW - w, top, w, 1 + 6 + 18 + 2 + 18 + 6 + 1);
        K::Fill(*c_, r, 8, K::Col(K::Token::canvas, alpha)); K::Border(*c_, r, 8, 1, PopupKit::White((0x14 / 255.f) * alpha));
        K::Text(*c_, t1, {r.left + 11, r.top + 7, r.right - 11, r.top + 25}, tip, PopupKit::White((0xE6 / 255.f) * alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        K::Text(*c_, t2, {r.left + 11, r.top + 27, r.right - 11, r.top + 45}, tip, PopupKit::White(.5f * alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
    }
    void Logo(A::AgentKind a, D2D1_RECT_F box, float alpha) {
        const auto& g = a == A::AgentKind::Claude ? A::Glyphs::claude : A::Glyphs::openai;
        glyphs_.Fill(*c_, g.d, box, g.w, a == A::AgentKind::Claude ? K::Col(0xD97757, alpha) : K::Col(K::Token::text, alpha), g.x, g.y);
    }
    // A3's row of an app: the modal's states at this row's size (h 40, radius 8, 14/18 500).
    void DrawApp(A::AgentKind a, D2D1_RECT_F r, int id, Motion::Time now, float alpha) {
        const auto& s = App(a); const K::Font f{14, 18, 500, 0}; const std::wstring label = AppLabel(a); const float lw = K::Width(*c_, label, f), cy = (r.top + r.bottom) / 2, cx = (r.left + r.right) / 2;
        const float glow = Enabled(id) ? glow_[id] : 0, press = Enabled(id) && pressed_ == id && hover_ == id ? 1.f : 0.f;
        const auto raised = K::Col(K::Lerp(K::Lerp(K::FromHex(K::Token::raised), K::FromHex(K::Token::hover), glow), K::FromHex(K::Token::pressed), press), alpha);
        auto text = [&](float x, D2D1_COLOR_F col) { K::Text(*c_, label, {x, cy - 9, x + lw + 4, cy + 9}, f, col); };
        switch (s.state) {
        case A::ConnectState::Connected: {
            K::Border(*c_, r, Layout::appRadius, 1, PopupKit::White((0x14 / 255.f) * alpha));
            const float x = std::round(cx - (16 + 6 + lw + 6 + 14) / 2);
            Logo(a, Box(x, cy - 8, 16, 16), alpha); text(x + 22, K::Col(K::Token::text, alpha));
            glyphs_.Fill(*c_, A::Glyphs::check.d, Box(x + 22 + lw + 6, cy - 7, 14, 14), A::Glyphs::check.w, K::Col(Cyan, alpha), A::Glyphs::check.x, A::Glyphs::check.y);
            break; }
        case A::ConnectState::Working: {
            K::Fill(*c_, r, Layout::appRadius, PopupKit::White((0x0A / 255.f) * alpha)); K::Border(*c_, r, Layout::appRadius, 1, PopupKit::White((0x14 / 255.f) * alpha));
            const float x = std::round(cx - (14 + 6 + lw) / 2); auto* t = c_->RenderTarget();
            D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Rotation(A::SpinnerAngle(now, options_.reduced || offscreen_), {x + 7, cy}) * base);
            glyphs_.Fill(*c_, A::Glyphs::ring.d, Box(x, cy - 7, 14, 14), A::Glyphs::ring.w, PopupKit::White((0xB3 / 255.f) * alpha), A::Glyphs::ring.x, A::Glyphs::ring.y);
            t->SetTransform(base);
            text(x + 20, PopupKit::White((0xCC / 255.f) * alpha));
            break; }
        default: {
            K::Fill(*c_, r, Layout::appRadius, raised);
            if (s.state == A::ConnectState::Failed) K::Border(*c_, r, Layout::appRadius, 1, K::Col(K::Token::danger, (0x66 / 255.f) * alpha));
            const float x = std::round(cx - (lw + 6 + 16) / 2);
            text(x, K::Col(K::Token::text, alpha)); Logo(a, Box(x + lw + 6, cy - 8, 16, 16), alpha);
        }
        }
        if (focus_ == id && keyboard_) PopupKit::InsetRing(*c_, r, Layout::appRadius);
    }
    void DrawAppMessage(A::AgentKind a, D2D1_RECT_F r, float alpha) {
        const std::wstring m = AppMessage(a); if (m.empty() || r.right <= r.left) return;
        const K::Font f{12, 16, 400, 0};
        if (App(a).state == A::ConnectState::Failed) {
            const float w = std::min(K::Width(*c_, m, f), r.right - r.left - 19), x = std::round((r.left + r.right) / 2 - (13 + 6 + w) / 2);
            glyphs_.Fill(*c_, A::Glyphs::warningCircle.d, Box(x, r.top + 1.5f, 13, 13), A::Glyphs::warningCircle.w, K::Col(K::Token::danger, alpha), A::Glyphs::warningCircle.x, A::Glyphs::warningCircle.y);
            K::Text(*c_, m, {x + 19, r.top, x + 19 + w + 1, r.bottom}, f, K::Col(K::Token::danger, alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        } else K::Text(*c_, m, r, f, PopupKit::White((0x99 / 255.f) * alpha), DWRITE_TEXT_ALIGNMENT_CENTER, false, true);
    }
    void DrawConnect(Motion::Time now, float alpha) {
        const auto k = CardLayout();
        K::Fill(*c_, k.card, Layout::cardRadius, K::Col(K::Token::canvas, alpha));
        DrawApp(A::AgentKind::Claude, k.claude, ClaudeRow, now, alpha); DrawAppMessage(A::AgentKind::Claude, k.claudeMsg, alpha);
        DrawApp(A::AgentKind::Codex, k.codex, CodexRow, now, alpha); DrawAppMessage(A::AgentKind::Codex, k.codexMsg, alpha);
        const float lift = glow_[Manual] * (0xB3 - 0x80) / 255.f;   // hover: the link comes up to the modal's #FFFFFFB3
        K::Text(*c_, TR(settings_manual_connect), {k.manual.left, k.manual.top, k.manual.right + 4, k.manual.bottom}, {12, 16, 400, 0}, PopupKit::White((0x80 / 255.f + lift) * alpha));
        if (focus_ == Manual && keyboard_) PopupKit::InsetRing(*c_, Rect(Manual), 6);
    }
    // A key cap: #171717 with a 3 DIP #0B0B0B bottom border, radius 12 (the inner edge's bottom corners 12 × 9).
    void DrawKey(D2D1_RECT_F r, const std::wstring& label, float alpha) {
        auto* t = c_->RenderTarget(); auto* b = c_->SolidBrush(); auto* f = c_->Factory(); if (!t || !b || !f) return;
        K::Fill(*c_, r, Layout::keyRadius, K::Col(K::Token::canvas, alpha));
        ID2D1RoundedRectangleGeometry* outer = nullptr; ID2D1PathGeometry* inner = nullptr, * ring = nullptr;
        if (SUCCEEDED(f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, Layout::keyRadius, Layout::keyRadius), &outer)) && SUCCEEDED(f->CreatePathGeometry(&inner)) && SUCCEEDED(f->CreatePathGeometry(&ring))) {
            ID2D1GeometrySink* s = nullptr; const float R = Layout::keyRadius, ry = R - Layout::keyBorder, bottom = r.bottom - Layout::keyBorder;
            if (SUCCEEDED(inner->Open(&s))) {
                s->BeginFigure({r.left, r.top + R}, D2D1_FIGURE_BEGIN_FILLED);
                s->AddArc(D2D1::ArcSegment({r.left + R, r.top}, {R, R}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                s->AddLine({r.right - R, r.top});
                s->AddArc(D2D1::ArcSegment({r.right, r.top + R}, {R, R}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                s->AddLine({r.right, bottom - ry});
                s->AddArc(D2D1::ArcSegment({r.right - R, bottom}, {R, ry}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                s->AddLine({r.left + R, bottom});
                s->AddArc(D2D1::ArcSegment({r.left, bottom - ry}, {R, ry}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); s->Release(); s = nullptr;
            }
            if (SUCCEEDED(ring->Open(&s))) { outer->CombineWithGeometry(inner, D2D1_COMBINE_MODE_EXCLUDE, nullptr, s); s->Close(); s->Release(); }
            b->SetColor(K::Col(0x0B0B0B, alpha)); t->FillGeometry(ring, b);
        }
        if (ring) ring->Release();
        if (inner) inner->Release();
        if (outer) outer->Release();
        // Centred in the 53 DIP above the border (14.5 down); measured on the Paper export, the glyphs sit one row lower
        // than a line box at 134.5 puts them (the « A » from y 140 to 154 at 100 %): the line box is set 15.5 down, on a
        // device pixel.
        const float top = std::floor((r.top + (r.bottom - Layout::keyBorder - r.top - 24) / 2 + 1) * Scale() + .5f) / Scale();
        K::Text(*c_, label, {r.left, top, r.right, r.bottom}, {20, 24, 500, 0}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    void DrawReady(float alpha) {
        const K::Font key{20, 24, 500, 0}, plus{18, 24, 500, 0};
        const std::wstring ctrl = L"Ctrl", shift = TR(key_shift), letter = L"A";   // i18n-exempt: key names (« Ctrl » and the letter are the same in every language)
        const float wc = 2 * Layout::keyPad + K::Width(*c_, ctrl, key), ws = 2 * Layout::keyPad + K::Width(*c_, shift, key), wp = K::Width(*c_, L"+", plus);
        const float total = wc + ws + Layout::keyA + 2 * wp + 4 * Layout::keyGap; float x = std::round(Layout::width / 2 - total / 2); const float top = Layout::keysTop;
        auto cap = [&](const std::wstring& label, float w) { DrawKey(Box(x, top, w, Layout::keyH), label, alpha); x += w + Layout::keyGap; };
        auto join = [&] { K::Text(*c_, L"+", {x, top + (Layout::keyH - 24) / 2, x + wp + 2, top + (Layout::keyH + 24) / 2}, plus, K::Col(0x0B3B3D, alpha)); x += wp + Layout::keyGap; };   // i18n-exempt: a symbol
        cap(ctrl, wc); join(); cap(shift, ws); join(); cap(letter, Layout::keyA);
        const auto r = Reader(); const K::Font f{13, 16, 500, 0};
        const std::wstring status = r == AO::Reader::None ? std::wstring(TR(annotation_no_agent_connected)) : TRF(connect_is_connected, AO::ReaderName(r));
        const float pw = Layout::pillPad + 8 + 8 + K::Width(*c_, status, f) + Layout::pillPad, py = top + Layout::keyH + Layout::pillGap;
        const D2D1_RECT_F pill = Box(std::round(Layout::width / 2 - pw / 2), py, pw, Layout::pillH);
        K::Fill(*c_, pill, 16, K::Col(K::Token::canvas, alpha));
        K::Fill(*c_, Box(pill.left + Layout::pillPad, py + 12, 8, 8), 4, r == AO::Reader::None ? PopupKit::White(.3f * alpha) : K::Col(Cyan, alpha));
        K::Text(*c_, status, {pill.left + Layout::pillPad + 16, py + 8, pill.right, py + 24}, f, PopupKit::White((0xE6 / 255.f) * alpha));
    }
    void DrawContent(Step s, Motion::Time now, float alpha) {
        if (alpha <= 0 || s == Step::Closed) return;
        switch (s) {
        case Step::What: P::AnnotationDemo(*c_, glyphs_, K::FromHex(Turquoise), alpha); break;
        case Step::Where: DrawWhere(alpha); break;
        case Step::Connect: DrawConnect(now, alpha); break;
        case Step::Ready: DrawReady(alpha); break;
        default: break;
        }
        DrawText(s, alpha); DrawDots(s, alpha); DrawButtons(s, alpha);
    }
    void Draw(Motion::Time now) {
        auto* t = c_->RenderTarget(); if (!t) return;
        P::TextLook(*c_, t);
        EnsureGpu();
        if (gpu_.Ready()) P::Shade(gpu_, grain_, t, Onboarding::Shaders::Kind::GrainGradient, P::GrainUniforms(ShaderSeconds(now), Seed()), P::GrainW, P::GrainH, ShadersLoop(), Due(now), 0, now);
        trame_.Shade(t, ShaderSeconds(now), Onboarding::Tuning::Defaults(Onboarding::Tuning::Trame), now);
        t->Clear(K::Col(0x000000));
        P::GrainBackdrop(*c_, grain_, trame_, Seed(), 1);
        K::Gradient(*c_, {-2, P::GrainVeilTop, 727, 579}, 0, {0, P::GrainVeilTop}, {0, 579}, {{P::GrainVeilFrom, K::FromHex(0x000000), 0}, {P::GrainVeilTo, K::FromHex(0x000000), 1}});
        const Step live = arranged_;
        const float raw = Change(now); const bool swapped = !changing_ || raw >= .4f; const Step content = swapped ? shown_ : from_;
        const float alpha = !changing_ ? 1.f : raw < .4f ? 1 - raw / .4f : (raw - .4f) / .6f;
        if (content != arranged_) Arrange(content);
        auto control = [&](int id, D2D1_RECT_F r) { return P::Control{r, .08f * glow_[id] + (pressed_ == id && hover_ == id ? .04f : 0) + (id == Lang && menuOpen_ ? .08f : 0), focus_ == id && keyboard_}; };
        P::HeaderControls(*c_, glyphs_, control(Lang, LangBox()), control(Close, CloseBox()), 1, 1);
        DrawContent(content, now, alpha);
        if (arranged_ != live) Arrange(live);
    }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return;
        t->BeginDraw(); Draw(Motion::Now());
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) { grain_.Drop(); trame_.Drop(); chrome_.RecreateTarget(); }
    }

    // ---- UI Automation ----
    void AttachUia() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; RECT win{}; GetWindowRect(hwnd_, &win); const Step s = flow_.step();
            auto bounds = [&](D2D1_RECT_F r) { return RECT{win.left + Px(r.left), win.top + Px(r.top), win.left + Px(r.right), win.top + Px(r.bottom)}; };
            Accessibility::Node g; g.key = 500; g.role = Accessibility::Role::Group; g.focusable = false; g.bounds = win; g.automationId = L"AnnotationOnboarding";
            g.name = Title(s); g.help = Body(s); std::replace(g.help.begin(), g.help.end(), L'\n', L' ');
            g.status = TRF(annotation_onboarding_step, std::to_wstring(AO::Index(s) + 1), std::to_wstring(AO::StepCount));
            nodes.push_back(g);
            if (s == Step::Closed) return nodes;
            auto button = [&](int id, const std::wstring& name, const wchar_t* automation) { Accessibility::Node n; n.key = id; n.parent = 500; n.role = Accessibility::Role::Button; n.name = name; n.automationId = automation; n.bounds = bounds(Rect(id)); n.enabled = Enabled(id); nodes.push_back(n); return nodes.size() - 1; };
            auto text = [&](long long key, const std::wstring& value) { Accessibility::Node n; n.key = key; n.parent = 500; n.role = Accessibility::Role::Text; n.name = value; n.focusable = false; nodes.push_back(n); };
            if (s == Step::Where) { text(600, TR(annotation_onboarding_local_only)); text(601, TR(annotation_onboarding_local_hint)); }
            if (s == Step::Connect) {
                for (A::AgentKind a : {A::AgentKind::Claude, A::AgentKind::Codex}) {
                    const int id = a == A::AgentKind::Claude ? ClaudeRow : CodexRow; const auto i = button(id, AppLabel(a), a == A::AgentKind::Claude ? L"AnnotationOnboardingClaude" : L"AnnotationOnboardingCodex");
                    const auto st = App(a).state; nodes[i].status = st == A::ConnectState::Working ? TR(demo_in_progress) : st == A::ConnectState::Connected ? TR(agent_connected) : st == A::ConnectState::Failed ? TR(downloads_failed) : L"";
                    nodes[i].help = AppMessage(a);
                }
                button(Manual, TR(settings_manual_connect), L"AnnotationOnboardingManual");
            }
            if (s == Step::Ready) { const auto r = Reader(); text(602, r == AO::Reader::None ? std::wstring(TR(annotation_no_agent_connected)) : TRF(connect_is_connected, AO::ReaderName(r))); }
            button(Secondary, SecondaryLabel(s), L"AnnotationOnboardingSecondary");
            const auto primary = button(Primary, PrimaryLabel(s), L"AnnotationOnboardingPrimary"); nodes[primary].accelerator = TR(key_enter);
            const auto lang = button(Lang, TR(language_label), L"AnnotationOnboardingLanguage"); nodes[lang].value = UiLanguage::Autonym(I18n::Current()); nodes[lang].expand = 1;
            const auto close = button(Close, TR(common_close), L"AnnotationOnboardingClose"); nodes[close].accelerator = TR(key_escape);
            return nodes;
        };
        host.focused = [this]() -> long long { return focus_; };
        host.invoke = [this](long long key) { Activate(static_cast<int>(key), true); };
        host.expand = [this](long long key, bool open) { if (open) Activate(static_cast<int>(key), true); };
        host.focus = [this](long long key) { Focus(static_cast<int>(key), true); };
        uia_.Attach(std::move(host));
    }

    // ---- window ----
    void Place(RECT client) {
        const int w = Px(Layout::width), h = Px(Layout::height);
        const int x = (client.left + client.right - w) / 2, y = std::max<int>(client.top + Px(16), (client.top + client.bottom - h) / 2);
        SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        NativeUI::ConfigureShadowlessPopup(hwnd_, Layout::radius); chrome_.Resize(); grain_.Drop(); trame_.Drop();
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        return self && self->hwnd_ == h ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    static LRESULT CALLBACK VeilProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATEANDEAT;
        if (m == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_ARROW)); return TRUE; }
        if (m == WM_ERASEBKGND) { RECT r; GetClientRect(h, &r); FillRect(reinterpret_cast<HDC>(w), &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))); return 1; }
        return DefWindowProcW(h, m, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = Scale();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case FrameMessage: if (l == FrameClock::ClockLost) { clock_.Stop(); clockOk_ = false; SetTimer(h, 1, 16, nullptr); return 0; } clock_.Acknowledge(); Step_(); return 0;
        case WM_TIMER: if (w == 1) Step_(); return 0;
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p);
            if (p.y / s < 48 && Hit(p.x / s, p.y / s) == None) return HTCAPTION;   // the header moves the window
            return HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr);
            const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s);
            if (id != hover_) { hover_ = id; if (id) glow_[id]; Invalidate(); RequestFrame(); }
            return 0;
        }
        case WM_MOUSELEAVE: if (hover_) { hover_ = None; Invalidate(); RequestFrame(); } return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: { const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); keyboard_ = false; SetFocus(h); SetCapture(h); pressed_ = id; if (id) focus_ = id; Invalidate(); return 0; }
        case WM_LBUTTONUP: {
            const int was = pressed_; pressed_ = None; if (GetCapture() == h) ReleaseCapture();
            const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); Invalidate();
            if (id && id == was) Activate(id, false);
            return 0;
        }
        case WM_CAPTURECHANGED: pressed_ = None; Invalidate(); return 0;
        case WM_KEYDOWN: Key(w); return 0;
        case WM_SYSKEYDOWN: if (w == VK_F4) { Activate(Close, true); return 0; } break;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE && open_) SetFocus(h); return 0;
        case WM_DPICHANGED: { dpi_ = HIWORD(w); const RECT* r = reinterpret_cast<const RECT*>(l); SetWindowPos(h, nullptr, r->left, r->top, Px(Layout::width), Px(Layout::height), SWP_NOZORDER | SWP_NOACTIVATE); NativeUI::ConfigureShadowlessPopup(h, Layout::radius); chrome_.Resize(); grain_.Drop(); trame_.Drop(); Arrange(); Invalidate(); return 0; }
        case WM_CLOSE: Activate(Close, false); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    Window() = default; Window(const Window&) = delete; Window& operator=(const Window&) = delete;
    ~Window() { if (open_) { silent_ = true; Finish(); } }
    HWND Handle() const { return hwnd_; }
    bool Visible() const { return open_; }
    const AO::Flow& Flow() const { return flow_; }
    // Opens over the browser: `owner` its window, `ownerClient` its client area in screen pixels (centring, veil).
    // `root`: the folder that holds assets\. False: nothing shown (the host falls back to the « Connecter un agent » modal).
    bool Open(HINSTANCE instance, HWND owner, const std::wstring& root, RECT ownerClient, Options options) {
        if (open_) return true;
        instance_ = instance; owner_ = owner; root_ = root; options_ = std::move(options); claude_ = options_.claude; codex_ = options_.codex;
        flow_ = AO::Flow{}; manual_ = silent_ = false; offscreen_ = false; focus_ = hover_ = pressed_ = None; keyboard_ = false; glow_.clear();
        if (!PopupKit::Register(instance, L"BrowserAnnotationOnboarding", Proc) || !PopupKit::Register(instance, L"BrowserAnnotationOnboardingVeil", VeilProc)) return false;
        veil_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"BrowserAnnotationOnboardingVeil", L"", WS_POPUP, ownerClient.left, ownerClient.top, ownerClient.right - ownerClient.left, ownerClient.bottom - ownerClient.top, owner, nullptr, instance, nullptr);
        if (veil_) SetLayeredWindowAttributes(veil_, 0, 0, LWA_ALPHA);
        hwnd_ = PopupKit::CreatePopup(instance, owner, L"BrowserAnnotationOnboarding", Title(Step::What).c_str(), 0, this, Layout::radius);
        if (!hwnd_) { if (veil_) { DestroyWindow(veil_); veil_ = nullptr; } return false; }
        dpi_ = owner ? GetDpiForWindow(owner) : 96; if (!dpi_) dpi_ = 96;
        if (FAILED(chrome_.Init(hwnd_, root_))) { Dispose(); return false; }
        Place(ownerClient); dpi_ = GetDpiForWindow(hwnd_) ? GetDpiForWindow(hwnd_) : dpi_; Place(ownerClient);
        menu_.Init(instance, hwnd_, root_);
        clockOk_ = clock_.Start(hwnd_, FrameMessage);
        open_ = true; shown_ = from_ = Step::What; changing_ = leaving_ = false; Arrange(); AttachUia();
        Paint();   // the first frame builds the shader: before the window shows
        opened_ = Motion::Now(); lastStep_ = {}; entering_ = true;
        if (veil_) ShowWindow(veil_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        if (owner_) EnableWindow(owner_, FALSE);   // modal: the browser behind waits, as for « Connecter un agent »
        SetForegroundWindow(hwnd_); SetFocus(hwnd_); RequestFrame();
        if (uia_.Attached()) uia_.Raise(500, UIA_Window_WindowOpenedEventId);
        Announce(Title(Step::What) + L". " + Body(Step::What));
        return true;
    }
    // The mode ended under it (the page went away): it goes at once and the host is not told (nothing was answered).
    void Dismiss() { if (!open_) return; silent_ = true; leaving_ = false; Finish(); }
    // From the host's connection work (A3's click) or its detection of the agents.
    void SetStatus(A::AgentKind a, const A::AppStatus& status) {
        if (a == A::AgentKind::Other) return;
        A::AppStatus& slot = a == A::AgentKind::Claude ? claude_ : codex_;
        if (slot.state == status.state && slot.detail == status.detail) return;
        slot = status;
        if (focus_ == ClaudeRow && !Enabled(ClaudeRow)) focus_ = keyboard_ ? Primary : None;
        if (focus_ == CodexRow && !Enabled(CodexRow)) focus_ = keyboard_ ? Primary : None;
        Arrange(); Invalidate(); RequestFrame();
        if (uia_.Attached()) uia_.ChildrenChanged(500);
        if (flow_.step() == Step::Connect) {
            if (status.state == A::ConnectState::Connected) Announce(TRF(settings_announce_connected, A::AppLong(a)) + (status.detail.empty() ? L"" : L". " + status.detail));
            else if (status.state == A::ConnectState::Failed) Announce(TRF(connect_failed_announce, status.detail.empty() ? std::wstring(TR(connect_impossible)) : status.detail));
            else if (status.state == A::ConnectState::Working) Announce(AppLabel(a));
        }
    }
    const A::AppStatus& Status(A::AgentKind a) const { return App(a); }   // what A3 shows now

    // ---- tests: the same drawing code into an offscreen target, and the events without a window ----
    void SetupForTest(const std::wstring& root, Options options) {
        root_ = root; options_ = std::move(options); claude_ = options_.claude; codex_ = options_.codex; offscreen_ = true; open_ = true;
        flow_ = AO::Flow{}; manual_ = silent_ = false; shown_ = from_ = Step::What; changing_ = leaving_ = false; focus_ = hover_ = pressed_ = None; keyboard_ = false;
    }
    bool OpenForTest() const { return open_; }
    void ActivateForTest(int id) { Arrange(); Activate(id, true); }
    bool KeyForTest(WPARAM key) { Arrange(); return Key(key); }
    int FocusForTest() const { return focus_; }
    void SetShaderSecondsForTest(double seconds) { secondsForTest_ = seconds; }
    std::vector<std::pair<int, D2D1_RECT_F>> ItemsForTest(Chrome& chrome) { Chrome* keep = c_; c_ = &chrome; Arrange(); c_ = keep; std::vector<std::pair<int, D2D1_RECT_F>> out; for (const auto& i : items_) out.push_back({i.id, i.r}); return out; }
    std::wstring BodyForTest() const { return Body(flow_.step()); }
    bool RenderForTest(UINT dpi, const wchar_t* path) {
        offscreen_ = true; dpi_ = dpi;
        return PopupKit::RenderPng(root_, Layout::width, Layout::height, dpi, path, c_, [this] {
            auto* t = c_->RenderTarget(); if (!t) return;
            gpuTried_ = gpuTried_ && gpu_.Ready();   // the first render loads the shader input with this target's factory
            Arrange(); t->BeginDraw(); Draw(Motion::Now()); t->EndDraw(); grain_.Drop(); trame_.Drop(); glyphs_.Clear();
        });
    }
};

} // namespace AnnotationOnboardingUI
} // namespace NativeChrome
