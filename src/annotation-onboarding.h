#pragma once
#include "i18n.h"
// Annotation mode tutorial (#125 v2, validated by Val) — the window. Paper « Browser Design » › « #125 v2 — Tutoriel
// annotation (validé) »: A1 1RTD-0 Bienvenue, A2 1RVB-0 Essai : choisir, A3 1RZ5-0 Essai : écrire, A4 1RX8-0 Essai : résultat,
// A5 1S3J-0 Où ça marche, A6 « Ton agent » (page « Onboarding agents — Ton agent (maquette prête) », in the place of 1S4U-0
// Connecter), A7 1S62-0 Prêt (cyan); A2 – A4 again in violet (1SAY-0, 1S7Y-0, 1SDI-0). A6 alone is the « Ton agent »
// screen of the chat and of the band's reminder (Options::alone; OpenTonAgent, src/chat-ton-agent.h).
// Flow: src/annotation-onboarding-flow.h. Host: src/annotation-mode.h. Values with their nodes: design/annotation-onboarding-values.md.
//
// The first-run onboarding's window family (src/onboarding.h board 6, drawn by the same code: src/onboarding-parts.h):
//   720 × 580, radius 12, #000000, no border. GrainGradient 725 × 407 at (−2, −2) over #DEFDFF + board 6's dither; the
//   veil y 249 … 579. Header 48: « Français ⌄ » at (12, 10), close 28 at (680, 10), #474747. Text block, bottom at y 498:
//   title 32/38 500 −0.035 em white, 5, body 14/20 #B3B3B3 (one or two lines), centred. Footer 66 high at y 514: secondary
//   raised #292929 at x 19, primary accent right-aligned at 701 (A2, A3: « Passer l'essai », a text button 14/20 500
//   #FFFFFFB3), all h 32, radius 6, padding-inline 12, y 531. Progress: 7 pills h 6 radius 3, gap 6, centred on (360, 547):
//   the current one 16 wide #FFFFFF, the others 6 wide #FFFFFF40.
//   Colours follow the profile like the annotation mode (Val): the boards are drawn for the sample profile (200° / 88 %);
//   gradient, accent and inks move to the profile's hue and intensity (OnboardingKit::Rehue / Accent, as board 6);
//   « Personnel » (no colour) takes the mode's blue #3E67FF (Options::accent, gradient turned to its hue).
//
//   A1 board 6's illustration (1RTE-0 = 1CHL-0).
//   A2 – A4 the fake page « Boutique » (1S12-0): 420 × 236 at (150, 64) — (110, 64) on A3 —, #F4F3F1, radius 10, clipped.
//     Header 36 #FFFFFF, border-bottom 1 #00000012, padding 16: « Boutique » 13/16 600 #1C1C1C, « Site d'exemple » 11/14
//     #00000073. Body padding 16, gap 10: « Paiement » 18/22 600 −0.02 em #1C1C1C; two fields h 30 #FFFFFF border 1 #00000014
//     radius 6 padding 10, 12/16 #0000004D; « Payer 48,00 € » 160 × 36 (A4: 260) #1C1C1C radius 8, 13/16 600 white.
//     A2: the element under the pointer framed (2 px accent, accent 8 % fill, radius 2, 4 out) with its tag (h 18, padding
//     6, accent, top radius 3, 12/16 #FFFFFFE6); the mode's crosshair (32) at the pointer; « 1 Cliquez sur « Payer » »
//     (h 36, radius 18, #171717, padding 8 / 12, gap 8: disc 20 accent « 1 » 11/12 600, 13/16 500 white) at (350, 228).
//     A3: the button framed (no radius) with « button.btn-primary »; the note (board 6's, 305 wide) at (304, 196) with the
//     typed text 14/20 #FFFFFFE6 and the caret « | » in the profile's light ink (#0AF4FF on the board).
//     A4: the button 260 wide; under it the measure (1.5 accent lines, « 160 → 260 » 11/14 600 #007D83 on the board, gap 6,
//     h 16, 6 below); « Fait » (pill as above, disc with check 12) 12 right of the button.
//   A5 three rows (1S3K-0 at (120, 84), 480 wide, gap 14): localhost (on), « Fichier  …/boutique/index.html » (on),
//     lemonde.fr (off) + the tooltip « Seulement sur vos projets et fichiers locaux ».
//   A6 « Ton agent » (src/annotation-onboarding-agent.h, values design/ton-agent-values.md): the card 400 wide at x 160,
//     #171717, radius 16, padding 12, two tiles (gap 10) of radius 12 — at rest #1F1F1F inset 1 #2A2A2A, current #192A2A
//     inset 1.5 #2FD1D9, failed danger 10 % inset 1.5 danger: logo 34 (26 from the top), name 16/21 600, 10, the mark
//     (icon 14, 4, 13/13 500 #A6A6A6 — the light ink when current, danger for « Erreur », « ✓ Prêt » 13/17). Its top by
//     state: centred on y 220 (Choisir, À connecter), 123 (Installation), 140 (Prêt), 72 (Erreur: the two lines 12/16
//     #A6A6A6 and « Installer moi-même ↗ » 12/16 500 #E6E6E6 underlined, 12 under the tiles); it glides there in 220 ms.
//     The strong button is the consent (AgentStatus::UserClicked); « Installation… » dimmed (#1F4A4C, #FFFFFF99). The mark
//     « Installation… » animates with the Downloads button's morph (src/download-icon.h) in the light ink; at the end it
//     morphs into the check, or fades into the error icon (Paper 253G-0); reduced motion: still icons.
//   A7 the shortcut and the agent's status (v1 A4: the agents' one status, « Aucun agent prêt » or « X est prêt »), « Terminer ».
//
// Window: an owned popup centred over the browser's client area, over the « Connecter un agent » modal's veil
// (#0000008C); the browser waits behind it (disabled). Motion (the first-run onboarding's): in 150 ms (veil 200 ms),
// steps change in 220 ms (old content out, new in; the gradient and header stay; between the trial's steps the fake
// page stays and glides to its place), out 120 ms (180 at the end); the button grows 160 → 260 in 400 ms then « Fait »
// and the measure fade in (200 ms); reduced motion: 120 ms changes, no growth, still shaders. Hover 120 ms.
// Keyboard: Tab / Maj+Tab through the step's controls (A2: « Payer »; A3: the note, « Envoyer »; A6: the tiles (← → between
// them) and « Installer moi-même »), the footer, the language and close buttons; typing goes to A3's note; Entrée = the focused control
// (A3's note: « Envoyer »), else the main button; Espace = the focused control; Échap = « Plus tard ».
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
#include <shellapi.h>
#include "agent-status.h"                  // « Ton agent »: the agents' one status (design/ton-agent-interface.md)
#include "annotation-onboarding-agent.h"   // what A6 shows for it
#include "download-icon.h"                 // « Installation… »: the Downloads button's morph
#include "native-lottie.h"

// The « Ton agent » screen is this window (A6, and A6 alone). OpenTonAgent (src/ton-agent-entry.h) is DEFINED with the chat,
// in src/chat-ton-agent.h (included at the end of src/annotation-mode.h); this tells annotation-mode.h to drop its stand-in.
#define BROWSER_TON_AGENT_SCREEN 1

namespace NativeChrome {
namespace AnnotationOnboardingUI {

namespace K = OnboardingKit; namespace P = OnboardingParts; namespace AO = AnnotationOnboarding; namespace A = Annotations;
using AO::Step; using AO::Part;

namespace Layout {
inline constexpr float width = 720, height = 580, radius = 12;
inline constexpr float headerTop = 10, langLeft = 12, closeLeft = 680;                    // padding 10 / 12
inline constexpr float barTop = 531, barLeft = 19, barRight = 701;                       // footer 66 high at y 514, padding-inline 19
inline constexpr float textBottom = 498, titleH = 38, titleGap = 5, line = 20;          // the text block, 16 above the footer
inline constexpr float dotsCentre = 547, dot = 6, dotWide = 16, dotGap = 6;             // progress
// A2 – A4: the fake page (1S12-0) and what is drawn over it.
inline constexpr float siteW = 420, siteH = 236, siteTop = 64, siteLeft = 150, siteLeftWrite = 110, siteRadius = 10, siteHeader = 36, sitePad = 16;
inline constexpr float payH = 36, frameOut = 4, tagH = 18, pillH = 36, pillTop = 228, coachLeft = 350, doneGap = 12, noteLeft = 304, noteTop = 196;
// A5
inline constexpr float rowsLeft = 120, rowsTop = 84, rowsW = 480, rowH = 48, rowsGap = 14, rowRadius = 12, rowPadL = 16, rowPadR = 8;
inline constexpr float pickH = 32, pickPad = 10, pickRadius = 8, tipMax = 280;
// A6 « Ton agent »: TonAgent::Card (src/annotation-onboarding-agent.h)
// A7
inline constexpr float keysTop = 120, keyH = 56, keyPad = 18, keyGap = 10, keyRadius = 12, keyBorder = 3, keyA = 56, pillGap = 20, statusH = 32, statusPad = 12;
}
inline constexpr std::size_t MaxNote = 300;   // A3: what the note takes (the demo does not read it)

struct Hooks {
    std::function<void(A::AgentKind)> connect;                     // (before « Ton agent ») A6's MCP rows: never called now, kept for the host's code
    std::function<void(AO::Ending, bool manual)> closed;           // after the window left; `manual` is always false now (no « Autre app » link)
    std::function<void(I18n::Locale)> language;                    // the header's language menu: persist the choice
    std::function<void(const std::wstring&)> announce;             // screen reader notifications (host: the browser's)
    std::function<void(AgentStatus::Agent)> openChat;              // A6 « Ouvrir le chat »: after the window left and `closed`, the agent to use
    std::function<void(const std::wstring&)> openPage;             // A6 « Installer moi-même ↗ »: the official page (the window copies the command)
};
// What « Ouvrir le chat » and « Installer moi-même ↗ » do when the host gave no hook (the tutorial opened by
// src/annotation-mode.h): set once by the chat (src/chat-ton-agent.h). Empty: nothing (tests set the hooks).
struct Fallback { std::function<void(AgentStatus::Agent)> openChat; std::function<void(const std::wstring&)> openPage; };
inline Fallback& Fallbacks() { static Fallback f; return f; }
struct Options {
    bool reduced = false;
    Step start = Step::What;       // the welcome window's « Essayer en 1 minute » (E2) starts at A2
    bool alone = false;            // the « Ton agent » screen by itself (the chat, the band's reminder): A6 only, no progress
    Profiles::Profile tint;        // the colour the boards move to (the profile's; default: the boards' own)
    unsigned accent = 0;           // 0xRRGGBB when the fills must be exactly that colour (« Personnel »: the mode's #3E67FF); 0: from `tint`
    A::AppStatus claude, codex;    // (before « Ton agent ») A6's MCP rows: ignored — A6 and A7 read the agents' one status
    Hooks hooks;
    Options() { tint.hue = K::PaperHue; tint.intensity = K::PaperChroma; tint.neutral = false; }
};

// A6's icons (Paper, view box 112): ⤓ Installer / Installation…, 📶 Connecter, ☹ Erreur.
namespace TonGlyph {
inline constexpr char download[] = "M99.75 63v28a5.25 5.25 0 0 1-5.25 5.25H17.5a5.25 5.25 0 0 1-5.25-5.25V63a5.25 5.25 0 0 1 10.5 0v22.75H89.25V63a5.25 5.25 0 0 1 10.5 0Zm-47.464 3.714a5.25 5.25 0 0 0 7.437 0l17.5-17.5a5.25 5.25 0 0 0-7.437-7.437L61.25 50.313V14a5.25 5.25 0 0 0-10.5 0v36.313L42.214 41.786a5.25 5.25 0 0 0-7.437 7.437Z";
inline constexpr char wifi[] = "M63 89.25a7 7 0 1 1-7-7A7 7 0 0 1 63 89.25ZM104.829 36.711a77 77 0 0 0-97.658 0 5.25 5.25 0 1 0 6.663 8.115 66.5 66.5 0 0 1 84.332 0 5.25 5.25 0 1 0 6.663-8.115Zm-14.07 15.632a56 56 0 0 0-69.518 0 5.25 5.25 0 0 0 6.518 8.229 45.5 45.5 0 0 1 56.482 0 5.25 5.25 0 0 0 6.518-8.229ZM76.593 67.944a35.022 35.022 0 0 0-41.186 0 5.25 5.25 0 0 0 6.186 8.487 24.5 24.5 0 0 1 28.814 0 5.25 5.25 0 1 0 6.186-8.487Z";
inline constexpr char error[] = "M56 8.75A47.25 47.25 0 1 0 103.25 56 47.302 47.302 90 0 0 56 8.75Zm0 84a36.75 36.75 0 1 1 36.75-36.75A36.789 36.789 90 0 1 56 92.75Zm24.714-40.464a5.25 5.25 0 0 1-7.437 7.437L70.875 57.313l-2.411 2.414a5.25 5.25 0 0 1-7.437-7.437L63.438 49.875l-2.416-2.411a5.25 5.25 0 0 1 7.438-7.437L70.875 42.438l2.411-2.416a5.25 5.25 0 0 1 7.437 7.438L78.313 49.875Zm-29.75 7.437a5.25 5.25 0 0 1-7.437 0L41.125 57.313l-2.411 2.414a5.25 5.25 0 0 1-7.437-7.437L33.688 49.875l-2.416-2.411a5.25 5.25 0 0 1 7.438-7.437L41.125 42.438l2.411-2.416a5.25 5.25 0 0 1 7.437 7.438L48.563 49.875l2.414 2.411A5.25 5.25 0 0 1 50.964 59.714ZM63 78.75a7 7 0 1 1-7-7A7 7 0 0 1 63 78.75Z";
inline constexpr float side = 112;
}
// « Installation… » (Paper 253G-0): the Downloads button's morph — the same Lottie layers, frames and loop as the
// toolbar's icon (src/download-icon.h, assets/lottie/download-*.json) — with every layer in one ink, the light accent
// (#2FD1D9 on the boards): track and fill over the arrow's cut-out, then the arrow (or the success check morph).
class InstallGlyph {
    Lottie::Asset track_, fill_, arrow_, cut_, ok_, okCut_; std::wstring root_; unsigned side_ = 0; bool failed_ = false, okReady_ = false;
    struct Slot { ID2D1Bitmap* bitmap = nullptr; ID2D1RenderTarget* target = nullptr; long long key = -1; } slots_[2];
    std::vector<uint32_t> px_;
    bool Load(unsigned side) {
        const std::wstring dir = root_ + L"\\assets\\lottie\\";
        failed_ = !(track_.Load(dir + L"download-ring-track.json") && fill_.Load(dir + L"download-ring-fill.json") && arrow_.Load(dir + L"download-arrow.json") && cut_.Load(dir + L"download-arrow-cutout.json"))
                  || track_.FrameCount() != DownloadIcon::RingFrames || fill_.FrameCount() != DownloadIcon::RingFrames || arrow_.FrameCount() != DownloadIcon::ArrowFrames || cut_.FrameCount() != DownloadIcon::ArrowFrames;
        okReady_ = ok_.Load(dir + L"download-success.json") && okCut_.Load(dir + L"download-success-cutout.json") && ok_.FrameCount() == DownloadIcon::SuccessFrames && okCut_.FrameCount() == DownloadIcon::SuccessFrames;
        side_ = side; return !failed_;
    }
public:
    InstallGlyph() = default; InstallGlyph(const InstallGlyph&) = delete; InstallGlyph& operator=(const InstallGlyph&) = delete;
    ~InstallGlyph() { Drop(); }
    void Drop() { for (auto& s : slots_) { if (s.bitmap) s.bitmap->Release(); s = Slot{}; } }
    void SetRoot(const std::wstring& root) { if (root != root_) { root_ = root; side_ = 0; failed_ = false; Drop(); } }
    bool Success() const { return okReady_; }
    // The pose (ring / arrow frames, or the success frame >= 0) in `box` (its centre, 14 DIP). False: not drawn (no
    // assets): the caller draws the still glyph.
    bool Draw(ID2D1RenderTarget* t, int slot, D2D1_RECT_F box, float scale, const DownloadIcon::Pose& pose, K::Rgb8 ink, float alpha) {
        if (!t || root_.empty() || alpha <= 0 || slot < 0 || slot > 1) return false;
        const unsigned side = std::clamp(static_cast<unsigned>(std::ceil(14 * scale)), 1u, 64u);
        if (side != side_ && !Load(side)) { Drop(); return false; }
        if (failed_) return false;
        const bool success = pose.success >= 0 && okReady_;
        auto& s = slots_[slot];
        if (s.target != t) { if (s.bitmap) s.bitmap->Release(); s = Slot{}; s.target = t; }
        const long long key = (((static_cast<long long>(pose.ring) * 256 + (success ? 128 + pose.success : pose.arrow)) * 2 + (success ? 1 : 0)) << 24) | (static_cast<long long>(ink.r) << 16) | (ink.g << 8) | ink.b;
        if (!s.bitmap || key != s.key) {
            auto at = [&](Lottie::Asset& a, int frame, int frames) { return a.Sample(static_cast<double>(frame) / (frames - 1), side); };
            const int ring = success ? DownloadIcon::FillEnd : pose.ring;
            const auto* tr = at(track_, ring, DownloadIcon::RingFrames); const auto* fi = at(fill_, ring, DownloadIcon::RingFrames);
            const auto* ar = success ? at(ok_, pose.success, DownloadIcon::SuccessFrames) : at(arrow_, pose.arrow, DownloadIcon::ArrowFrames);
            const auto* cu = success ? at(okCut_, pose.success, DownloadIcon::SuccessFrames) : at(cut_, pose.arrow, DownloadIcon::ArrowFrames);
            if (!tr || !fi || !ar || !cu) return false;
            const std::size_t n = static_cast<std::size_t>(side) * side; px_.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                const float ta = (tr->bgra[i] >> 24) / 255.f, fa = (fi->bgra[i] >> 24) / 255.f, aa = (ar->bgra[i] >> 24) / 255.f, keep = 1 - (cu->bgra[i] >> 24) / 255.f;
                const float ringA = fa + ta * (1 - fa), a = std::clamp(aa + ringA * keep * (1 - aa), 0.f, 1.f);
                px_[i] = (static_cast<uint32_t>(std::lround(a * 255)) << 24) | (static_cast<uint32_t>(std::lround(ink.r * a)) << 16) | (static_cast<uint32_t>(std::lround(ink.g * a)) << 8) | static_cast<uint32_t>(std::lround(ink.b * a));
            }
            HRESULT hr = S_OK;
            if (!s.bitmap) hr = t->CreateBitmap(D2D1::SizeU(side, side), px_.data(), side * 4, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &s.bitmap);
            else hr = s.bitmap->CopyFromMemory(nullptr, px_.data(), side * 4);
            if (FAILED(hr) || !s.bitmap) { if (s.bitmap) s.bitmap->Release(); s.bitmap = nullptr; return false; }
            s.key = key;
        }
        const float x = (box.left + box.right) / 2 - 7, y = (box.top + box.bottom) / 2 - 7;
        t->DrawBitmap(s.bitmap, {x, y, x + 14, y + 14}, alpha);
        return true;
    }
};

class Window {
public:
    // A6: ClaudeRow / CodexRow are the two tiles, Manual the « Installer moi-même ↗ » link.
    enum Id : int { None = 0, Lang = 1, Close = 2, Primary = 10, Secondary = 11, ClaudeRow = 20, CodexRow = 21, Manual = 22, Payer = 30, Field = 31, Send = 32 };
    static constexpr UINT FrameMessage = WM_APP + 0x4E0;
    static constexpr UINT_PTR CaretTimer = 2;
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
    Motion::Time opened_{}, lastStep_{}, changed_{}, left_{}, grewAt_{};
    Step shown_ = Step::What, from_ = Step::What; bool changing_ = false, entering_ = false, leaving_ = false;
    Part part_ = Part::None; D2D1_POINT_2F pointer_{-100, -100}; bool caretOn_ = true;   // A2's hovered element and the pointer; A3's caret blink
    double secondsForTest_ = -1;
    // ---- A6 « Ton agent » ----
    using Agent = AgentStatus::Agent; using AState = AgentStatus::State;
    TonAgent::Screen pick_; int listener_ = 0; AgentStatus::Status seen_[2]; Agent chatAgent_ = Agent::Claude; int manualForTest_ = 0; std::wstring page_;
    DownloadIcon::Model install_[2]; Motion::Time ended_[2]{}; int endKind_[2] = {0, 0}; AState endTo_[2] = {AState::Unknown, AState::Unknown};   // the install's end: 1 the success morph, 2 the fade to the error icon
    float cardFrom_ = -1, cardTo_ = -1; Motion::Time cardAt_{};   // the card glides between its places (Paper: one per state)
    InstallGlyph installGlyph_;
    static constexpr float SuccessMs = DownloadIcon::SuccessMorph * 1000.f / 60, SuccessHoldMs = 600, FailMs = 150, GlideMs = 220;

    // ---- small helpers ----
    float Scale() const { return dpi_ / 96.f; }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Scale())); }
    static float Ms(Motion::Time from, Motion::Time to) { return static_cast<float>(std::chrono::duration<double, std::milli>(to - from).count()); }
    static float EaseOut(float u) { u = Motion::Clamp(u); const float v = 1 - u; return 1 - v * v * v * v; }
    static bool In(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
    static D2D1_RECT_F Box(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
    static D2D1_RECT_F Grow(D2D1_RECT_F r, float d) { return {r.left - d, r.top - d, r.right + d, r.bottom + d}; }
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    // ---- colours: the board's value carried to the profile ----
    const Profiles::Profile& Tint() const { return options_.tint; }
    K::Rgb8 Accent() const { return options_.accent ? K::FromHex(options_.accent) : K::Accent(Tint()); }
    unsigned AccentHex() const { const auto a = Accent(); return (unsigned(a.r) << 16) | (unsigned(a.g) << 8) | a.b; }
    K::Rgb8 Tone(unsigned paper) const { return K::Rehue(paper, Tint()); }   // #2FD1D9 (check, dot), #0AF4FF (caret), #007D83 (measure)
    const A::AppStatus& App(A::AgentKind a) const { return a == A::AgentKind::Claude ? claude_ : codex_; }
    // ---- A6 / A7: the agents' one status (src/agent-status.h) ----
    static const AgentStatus::Status& St(Agent a) { return AgentStatus::Get(a); }
    Agent Target() const { return pick_.Target(St(Agent::Claude), St(Agent::Codex)); }
    const AgentStatus::Status& TargetStatus() const { return St(Target()); }
    static std::wstring Name(Agent a) { return a == Agent::Codex ? L"Codex" : L"Claude Code"; }   // i18n-exempt: product names
    AO::Reader Reader() const { return AO::ReaderOf(St(Agent::Claude).Ready(), St(Agent::Codex).Ready()); }
    bool Alone() const { return flow_.alone(); }
    void Announce(const std::wstring& text) { if (text.empty()) return; if (options_.hooks.announce) options_.hooks.announce(text); else if (uia_.Attached()) uia_.Notify(500, text.c_str(), L"AnnotationOnboarding"); }   // i18n-exempt: UIA activity id

    // ---- texts of each step ----
    std::wstring Title(Step s) const {
        switch (s) {
        case Step::Pick: return TR(annotation_onboarding_pick_title); case Step::Write: return TR(annotation_onboarding_write_title); case Step::Result: return TR(annotation_onboarding_result_title);
        case Step::Where: return TR(annotation_onboarding_where_title); case Step::Connect: return TonTitle(); case Step::Ready: return TR(annotation_onboarding_ready_title);
        default: return TR(annotation_onboarding_what_title);
        }
    }
    std::wstring Body(Step s) const {
        switch (s) {
        case Step::Pick: return TR(annotation_onboarding_pick_body); case Step::Write: return TR(annotation_onboarding_write_body); case Step::Result: return TR(annotation_onboarding_result_body);
        case Step::Where: return TR(annotation_onboarding_where_body);
        case Step::Connect: return TonBody();
        case Step::Ready: { const auto r = Reader(); return TRF(annotation_onboarding_ready_body, r == AO::Reader::None ? std::wstring(TR(annotation_onboarding_your_agent)) : std::wstring(AO::ReaderName(r))); }
        default: return TR(annotation_onboarding_what_body);
        }
    }
    // A6: « Choisissez votre agent », « Installation de X », « Connectez X », « X est prêt » (the target's state).
    std::wstring TonTitle() const {
        const std::wstring n = Name(Target());
        switch (TonAgent::HeadingOf(TargetStatus())) {
        case TonAgent::Heading::Installing: return TRF(ton_agent_installing_title, n);
        case TonAgent::Heading::SignIn: return TRF(ton_agent_signin_title, n);
        case TonAgent::Heading::Ready: return TRF(ton_agent_ready_title, n);
        default: return TR(ton_agent_choose_title);
        }
    }
    std::wstring TonBody() const {
        switch (TonAgent::HeadingOf(TargetStatus())) {
        case TonAgent::Heading::Installing: return TR(ton_agent_installing_body);
        case TonAgent::Heading::SignIn: return Target() == Agent::Codex ? TR(ton_agent_signin_codex_body) : TR(ton_agent_signin_claude_body);   // the paid Claude plan: said here
        case TonAgent::Heading::Ready: return TR(ton_agent_ready_body);
        default: return TR(ton_agent_choose_body);
        }
    }
    std::wstring TonButton() const {
        const std::wstring n = Name(Target());
        switch (TonAgent::ButtonOf(TargetStatus()).label) {
        case TonAgent::Label::Installing: return TR(ton_agent_button_installing);
        case TonAgent::Label::SignIn: return TRF(ton_agent_button_signin, n);
        case TonAgent::Label::OpenChat: return TR(ton_agent_button_open_chat);
        case TonAgent::Label::Retry: return TR(ton_agent_button_retry);
        default: return TRF(ton_agent_button_install, n);
        }
    }
    std::wstring MarkWord(const AgentStatus::Status& s) const {
        switch (TonAgent::MarkOf(s)) {
        case TonAgent::Mark::Install: return TR(ton_agent_mark_install); case TonAgent::Mark::Installing: return TR(ton_agent_mark_installing);
        case TonAgent::Mark::Connect: return TR(ton_agent_mark_connect); case TonAgent::Mark::Ready: return TR(ton_agent_mark_ready);
        case TonAgent::Mark::Error: return TR(ton_agent_mark_error); default: return L"";
        }
    }
    std::wstring TroubleText() const { const auto t = TonAgent::TroubleOf(TargetStatus()); return t == TonAgent::Trouble::SignIn ? TR(ton_agent_signin_failed) : t == TonAgent::Trouble::Install ? TR(ton_agent_install_failed) : L""; }
    bool PrimaryDimmed(Step s) const { return s == Step::Connect && TonAgent::ButtonOf(TargetStatus()).dimmed; }
    static int Lines(const std::wstring& text) { return 1 + static_cast<int>(std::count(text.begin(), text.end(), L'\n')); }
    std::wstring SecondaryLabel(Step s) const {
        if (s == Step::Connect) return !Alone() && TargetStatus().Ready() ? TR(common_back) : TR(common_later);   // A6: « Plus tard » (to A7), « Retour » once ready
        return s == Step::What ? TR(common_later) : s == Step::Result ? TR(annotation_onboarding_restart) : TR(common_back);
    }
    std::wstring PrimaryLabel(Step s) const {
        switch (s) {
        case Step::Pick: case Step::Write: return TR(annotation_onboarding_skip_trial);
        case Step::Connect: return TonButton();
        case Step::Ready: return TR(annotation_onboarding_finish);
        default: return TR(annotation_onboarding_next);
        }
    }
    static bool TextPrimary(Step s) { return s == Step::Pick || s == Step::Write; }   // « Passer l'essai »: no fill

    // ---- the fake page (A2 – A4) ----
    static float SiteLeft(Step s) { return s == Step::Write ? Layout::siteLeftWrite : Layout::siteLeft; }
    // The button's width: 160, growing to 260 on A4 (instantly off screen and with reduced motion).
    float PayWidth(Step s, Motion::Time now) const {
        if (s != Step::Result) return AO::PayFrom;
        if (offscreen_ || options_.reduced || grewAt_ == Motion::Time{}) return AO::PayTo;
        return AO::PayFrom + (AO::PayTo - AO::PayFrom) * EaseOut(Ms(grewAt_, now) / AO::GrowMs);
    }
    float DoneShown(Motion::Time now) const { return offscreen_ || options_.reduced || grewAt_ == Motion::Time{} ? 1.f : Motion::Clamp((Ms(grewAt_, now) - AO::GrowMs) / AO::ShowDoneMs); }
    D2D1_RECT_F SiteBox(float left) const { return Box(left, Layout::siteTop, Layout::siteW, Layout::siteH); }
    D2D1_RECT_F PartBox(Part p, float left, float payW) {
        const float x = left + Layout::sitePad, top = Layout::siteTop + Layout::siteHeader + Layout::sitePad, w = Layout::siteW - 2 * Layout::sitePad;
        switch (p) {
        case Part::Header: return Box(left, Layout::siteTop, Layout::siteW, Layout::siteHeader);
        case Part::Title: return Box(x, top, K::Width(*c_, TR(annotation_onboarding_site_pay_title), {18, 22, 600, -.36f}), 22);
        case Part::Email: return Box(x, top + 32, w, 30);
        case Part::Card: return Box(x, top + 72, w, 30);
        case Part::Pay: return Box(x, top + 112, payW, Layout::payH);
        default: return {};
        }
    }
    Part PartAt(float x, float y) {
        if (!In(SiteBox(Layout::siteLeft), x, y)) return Part::None;
        for (Part p : {Part::Pay, Part::Card, Part::Email, Part::Title, Part::Header}) if (In(PartBox(p, Layout::siteLeft, AO::PayFrom), x, y)) return p;
        return Part::None;
    }
    // A3's note: the typed text (or the grey hint) wraps in 281; the card grows with it.
    float NoteTextHeight() { return std::max(20.f, K::WrappedHeight(*c_, (flow_.text().empty() ? std::wstring(TR(annotation_onboarding_write_hint)) : flow_.text()) + L"|", 281, {14, 20, 400, 0})); }
    D2D1_RECT_F NoteField() { return Box(Layout::noteLeft + 12, Layout::noteTop + 12, 281, NoteTextHeight()); }

    // ---- geometry of the interactive parts ----
    D2D1_RECT_F LangBox() { return Box(Layout::langLeft, Layout::headerTop, P::LanguageWidth(*c_), P::HeaderSize); }
    static D2D1_RECT_F CloseBox() { return Box(Layout::closeLeft, Layout::headerTop, P::HeaderSize, P::HeaderSize); }
    D2D1_RECT_F SecondaryBox(Step s) { return Box(Layout::barLeft, Layout::barTop, P::ButtonWidth(*c_, SecondaryLabel(s), 500), P::ButtonHeight); }
    D2D1_RECT_F PrimaryBox(Step s) { const float w = P::ButtonWidth(*c_, PrimaryLabel(s), 500); return Box(Layout::barRight - w, Layout::barTop, w, P::ButtonHeight); }
    // A6's card at `top`: the tiles, the two lines of a failure and « Installer moi-même ↗ ».
    struct TonCard { D2D1_RECT_F card, tile[2], trouble, manual; TonAgent::Trouble kind; };
    static TonAgent::Mark MarkNow(Agent a) { return TonAgent::MarkOf(St(a)); }
    float TonHeight() const { return TonAgent::Card::Height(MarkNow(Agent::Claude), MarkNow(Agent::Codex), TonAgent::TroubleOf(TargetStatus())); }
    float TonTop() const { return TonAgent::Card::Top(TargetStatus(), TonHeight()); }
    float CardTopAt(Motion::Time now) const {
        if (cardTo_ < 0) return TonTop();
        if (cardFrom_ < 0 || offscreen_ || options_.reduced) return cardTo_;
        return cardFrom_ + (cardTo_ - cardFrom_) * EaseOut(Ms(cardAt_, now) / GlideMs);
    }
    bool CardGliding() const { return cardFrom_ >= 0 && !offscreen_ && !options_.reduced && Ms(cardAt_, Motion::Now()) < GlideMs; }
    // The card goes to its place for the state (from where it is shown now).
    void GlideCard(Motion::Time now) {
        const float want = TonTop();
        if (cardTo_ < 0 || offscreen_ || options_.reduced || flow_.step() != Step::Connect) { cardTo_ = want; cardFrom_ = -1; return; }
        if (std::fabs(want - cardTo_) < .01f) return;
        cardFrom_ = CardTopAt(now); cardTo_ = want; cardAt_ = now;
    }
    TonCard TonLayout(float top) {
        namespace C = TonAgent::Card; TonCard k{};
        const float tileW = (C::width - 2 * C::pad - C::gap) / 2, tileH = C::TileHeight(MarkNow(Agent::Claude), MarkNow(Agent::Codex));
        k.tile[0] = Box(C::left + C::pad, top + C::pad, tileW, tileH); k.tile[1] = Box(C::left + C::pad + tileW + C::gap, top + C::pad, tileW, tileH);
        k.kind = TonAgent::TroubleOf(TargetStatus());
        float bottom = top + C::pad + tileH + C::pad;
        if (k.kind != TonAgent::Trouble::None) {
            const float y = top + C::pad + tileH + C::troubleGap;
            k.trouble = Box(C::left + C::pad, y, C::width - 2 * C::pad, 2 * C::troubleLine);
            if (k.kind == TonAgent::Trouble::Install) { const float w = K::Width(*c_, TR(ton_agent_manual), {12, 16, 500, 0}); k.manual = Box(std::round(Layout::width / 2 - w / 2), y + 2 * C::troubleLine + C::troubleLinkGap, w, C::troubleLine); }
            bottom = top + C::Height(MarkNow(Agent::Claude), MarkNow(Agent::Codex), k.kind);
        }
        k.card = {C::left, top, C::left + C::width, bottom};
        return k;
    }
    void Arrange() { Arrange(flow_.step()); }
    void Arrange(Step s) {
        items_.clear(); arranged_ = s;
        if (s == Step::Closed) return;
        if (s == Step::Pick) items_.push_back({Payer, PartBox(Part::Pay, Layout::siteLeft, AO::PayFrom)});
        if (s == Step::Write) { items_.push_back({Field, Grow(NoteField(), 4)}); items_.push_back({Send, P::DemoNoteSend(Layout::noteLeft, Layout::noteTop, NoteTextHeight())}); }
        if (s == Step::Connect) {
            const auto k = TonLayout(std::floor(CardTopAt(Motion::Now()))); items_.push_back({ClaudeRow, k.tile[0]}); items_.push_back({CodexRow, k.tile[1]});
            if (k.kind == TonAgent::Trouble::Install) items_.push_back({Manual, {k.manual.left - 6, k.manual.top - 4, k.manual.right + 6, k.manual.bottom + 4}});
        }
        items_.push_back({Secondary, SecondaryBox(s)}); items_.push_back({Primary, PrimaryBox(s)});
        items_.push_back({Lang, LangBox()}); items_.push_back({Close, CloseBox()});
    }
    D2D1_RECT_F Rect(int id) const { for (const auto& i : items_) if (i.id == id) return i.r; return {}; }
    bool Enabled(int id) const {
        if (id == Manual) return flow_.step() == Step::Connect && TonAgent::ManualOffered(TargetStatus());
        if (id == Primary) return !PrimaryDimmed(flow_.step());   // « Installation… »: nothing to click until the end
        if (id == Send) return flow_.CanSend();
        return true;
    }
    int Hit(float x, float y) const { if (leaving_) return None; for (auto it = items_.rbegin(); it != items_.rend(); ++it) if (In(it->r, x, y)) return Enabled(it->id) || it->id == Send ? it->id : None; return None; }
    std::vector<int> TabOrder() const {
        std::vector<int> o; const Step s = flow_.step(); if (s == Step::Closed) return o;
        if (s == Step::Pick) o.push_back(Payer);
        if (s == Step::Write) { o.push_back(Field); if (Enabled(Send)) o.push_back(Send); }
        if (s == Step::Connect) { o.push_back(ClaudeRow); o.push_back(CodexRow); if (Enabled(Manual)) o.push_back(Manual); }
        o.push_back(Secondary); if (Enabled(Primary)) o.push_back(Primary); o.push_back(Lang); o.push_back(Close);
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
    // A6 moves: the card gliding, an « Installation… » morph looping, an install's end (success morph, fade to the error).
    float EndMs(int i) const { return endKind_[i] == 2 ? FailMs : endTo_[i] == AState::Ready ? SuccessMs : SuccessMs + SuccessHoldMs; }
    bool Ending(int i, Motion::Time now) const { return endKind_[i] != 0 && Ms(ended_[i], now) < EndMs(i); }
    bool TonMoving() const {
        if (offscreen_ || (flow_.step() != Step::Connect && shown_ != Step::Connect)) return false;
        if (CardGliding()) return true;
        if (options_.reduced) return false;
        const auto now = Motion::Now();
        for (int i = 0; i < 2; ++i) if (St(TonAgent::AllAgentsAt(i)).state == AState::Installing || Ending(i, now)) return true;
        return false;
    }
    bool Growing() const { return flow_.step() == Step::Result && grewAt_ != Motion::Time{} && Ms(grewAt_, Motion::Now()) < AO::GrowMs + AO::ShowDoneMs + 20; }
    bool Animating() const {
        if (changing_ || entering_ || leaving_ || TonMoving() || Growing()) return true;
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
    void Blink(bool on) {   // A3's caret: 530 ms on / off while the note is there
        caretOn_ = true;
        if (!hwnd_ || offscreen_) return;
        if (on) SetTimer(hwnd_, CaretTimer, 530, nullptr); else KillTimer(hwnd_, CaretTimer);
    }

    // ---- flow events ----
    void Changed(Step before) {
        const Step now = flow_.step();
        if (now == before) { Arrange(); Invalidate(); return; }
        if (now == Step::Closed) { Blink(false); Leave(); return; }
        from_ = shown_; shown_ = now; changed_ = Motion::Now(); changing_ = !offscreen_;
        grewAt_ = now == Step::Result ? changed_ : Motion::Time{};
        focus_ = now == Step::Write ? static_cast<int>(Field) : keyboard_ ? static_cast<int>(Primary) : static_cast<int>(None); hover_ = None; pressed_ = None; part_ = Part::None;
        Blink(now == Step::Write);
        if (now == Step::Connect) { cardTo_ = -1; GlideCard(changed_); AgentStatus::Refresh(); }   // the screen shown: the agents detected again
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
        if (hwnd_) { clock_.Stop(); KillTimer(hwnd_, 1); KillTimer(hwnd_, CaretTimer); ShowWindow(hwnd_, SW_HIDE); }
        if (veil_) ShowWindow(veil_, SW_HIDE);
        Dispose();
        const AO::Ending ending = flow_.ending(); const bool manual = manual_, silent = silent_, chat = flow_.chat(); const Agent who = chatAgent_;
        const auto openChat = options_.hooks.openChat ? options_.hooks.openChat : Fallbacks().openChat;   // copied: `closed` may reopen this window
        const auto openPage = options_.hooks.openPage ? options_.hooks.openPage : Fallbacks().openPage; const std::wstring page = page_; page_.clear();
        if (!silent && options_.hooks.closed) options_.hooks.closed(ending, manual);
        if (!silent && chat && openChat) openChat(who);   // A6 « Ouvrir le chat »: once the window is gone and the host knows
        if (!silent && !page.empty()) { if (openPage) openPage(page); else if (!offscreen_) ShellExecuteW(nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }   // « Installer moi-même ↗ »
    }
    void Dispose() {
        Unlisten(); installGlyph_.Drop();
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
        case Secondary:
            if (before == Step::What) flow_.Skip();
            else if (before == Step::Connect) { if (Alone()) flow_.Skip(); else if (TargetStatus().Ready()) flow_.Back(); else flow_.Later(); }   // A6: « Plus tard » → A7, « Retour » once ready
            else flow_.Back();
            break;
        case Primary: if (before == Step::Connect) { if (!TonPrimary()) return; break; } flow_.Next(); break;
        case Payer: flow_.Pick(Part::Pay); break;
        case Field: focus_ = Field; Invalidate(); return;
        case Send: flow_.Send(); break;
        case ClaudeRow: case CodexRow: if (before == Step::Connect) PickTile(id == ClaudeRow ? Agent::Claude : Agent::Codex); return;
        case Manual: if (before != Step::Connect) return; InstallMyself(); break;   // the screen goes, the official page shows
        default: return;
        }
        Changed(before);
    }
    // ---- A6 « Ton agent » ----
    // The strong button. Its click IS the consent of an install (AgentStatus::UserClicked): only here. True: the flow
    // moved (« Ouvrir le chat »), the caller follows it.
    bool TonPrimary() {
        const Agent a = Target(); const auto b = TonAgent::ButtonOf(St(a));
        switch (b.action) {
        case TonAgent::Action::Install: pick_.Pick(a); AgentStatus::Install(AgentStatus::UserClicked(a)); break;
        case TonAgent::Action::SignIn: pick_.Pick(a); AgentStatus::SignIn(a); break;
        case TonAgent::Action::Retry: pick_.Pick(a); AgentStatus::Retry(AgentStatus::UserClicked(a)); break;
        case TonAgent::Action::OpenChat: chatAgent_ = a; return flow_.OpenChat();
        default: return false;
        }
        GlideCard(Motion::Now()); Arrange(); Invalidate(); RequestFrame();
        return false;
    }
    // A click on a tile: that agent is the target (the strong button and the text follow).
    void PickTile(Agent a) {
        const Agent was = Target(); pick_.Pick(a);
        GlideCard(Motion::Now()); Arrange(); Invalidate(); RequestFrame();
        if (uia_.Attached()) uia_.ChildrenChanged(500);
        if (Target() != was) Announce(TonTitle() + L". " + TonBody() + L". " + TonButton());
    }
    // « Installer moi-même ↗ »: the official command to the clipboard (to paste in PowerShell); the screen closes (skipped)
    // and the official page opens once it is gone (in a tab of the browser: the modal would hide it).
    void InstallMyself() {
        const auto m = AgentStatus::ManualInstall(Target()); ++manualForTest_; page_ = m.url;
        if (hwnd_ && !offscreen_ && OpenClipboard(hwnd_)) {
            EmptyClipboard(); const std::wstring text = m.command; const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) { if (void* p = GlobalLock(g)) { memcpy(p, text.c_str(), bytes); GlobalUnlock(g); if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g); } else GlobalFree(g); }
            CloseClipboard();
        }
        Announce(TR(ton_agent_manual_done));
        flow_.Skip();
    }
    // The agents' status changed (a listener, UI thread): the tiles, the text, the button and the card's place follow;
    // an install that began starts the morph, one that ended plays its end.
    void OnStatus(const AgentStatus::Status& s) {
        const int i = TonAgent::Index(s.agent); const AState was = seen_[i].state; const auto now = Motion::Now();
        if (s.state == AState::Installing && was != AState::Installing) { install_[i] = DownloadIcon::Model{}; install_[i].Update(true, 0, false, now, options_.reduced || offscreen_); endKind_[i] = 0; }
        if (was == AState::Installing && s.state != AState::Installing) { install_[i] = DownloadIcon::Model{}; endKind_[i] = s.state == AState::Error || s.state == AState::NotInstalled ? 2 : 1; endTo_[i] = s.state; ended_[i] = now; }
        seen_[i] = s;
        if (!open_) return;
        GlideCard(now);
        if (focus_ == Manual && !Enabled(Manual)) focus_ = keyboard_ ? static_cast<int>(Secondary) : static_cast<int>(None);
        Arrange(); Invalidate(); RequestFrame();
        if (uia_.Attached()) uia_.ChildrenChanged(500);
        if (flow_.step() == Step::Connect && s.agent == Target() && was != s.state && s.state != AState::Unknown) {
            if (hwnd_) SetWindowTextW(hwnd_, TonTitle().c_str());
            Announce(s.state == AState::Error ? TonTitle() + L". " + TroubleText() : TonTitle() + L". " + TonBody());
        }
    }
    void Listen() {
        for (Agent a : AgentStatus::AllAgents) seen_[TonAgent::Index(a)] = St(a);
        if (!listener_) listener_ = AgentStatus::Subscribe([this](const AgentStatus::Status& s) { OnStatus(s); });
    }
    void Unlisten() { if (listener_) { AgentStatus::Unsubscribe(listener_); listener_ = 0; } }
    void Focus(int id, bool keyboard) { keyboard_ = keyboard; focus_ = id; if (hwnd_ && GetFocus() != hwnd_ && !offscreen_) SetFocus(hwnd_); Invalidate(); if (uia_.Attached()) uia_.Raise(id, UIA_AutomationFocusChangedEventId); }
    void TabTo(bool back) {
        const auto order = TabOrder(); if (order.empty()) return;
        auto it = std::find(order.begin(), order.end(), focus_); int next;
        if (it == order.end()) next = back ? order.back() : order.front();
        else if (back) next = it == order.begin() ? order.back() : *(it - 1);
        else next = it + 1 == order.end() ? order.front() : *(it + 1);
        Focus(next, true);
    }
    // A3's note: what typing does to it (the window keeps the text; no native EDIT: the caret is the board's « | »).
    void Type(const std::wstring& text) {
        if (flow_.step() != Step::Write) return;
        std::wstring t = flow_.text();
        for (wchar_t ch : text) { if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' '; if (ch >= 0x20 && t.size() < MaxNote) t += ch; }
        const bool could = flow_.CanSend(); flow_.SetText(t); caretOn_ = true; Blink(true);
        Arrange(); Invalidate();
        if (could != flow_.CanSend() && uia_.Attached()) uia_.ChildrenChanged(500);
    }
    void Erase() {
        if (flow_.step() != Step::Write || flow_.text().empty()) return;
        std::wstring t = flow_.text(); t.pop_back(); if (!t.empty() && IS_HIGH_SURROGATE(t.back())) t.pop_back();
        flow_.SetText(t); caretOn_ = true; Blink(true); Arrange(); Invalidate();
    }
    void Paste() {
        if (offscreen_ || !hwnd_ || !OpenClipboard(hwnd_)) return;
        std::wstring text;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) { text = p; GlobalUnlock(h); }
        CloseClipboard(); Type(text);
    }
    bool Key(WPARAM key) {
        keyboard_ = true; const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
        if (key == VK_ESCAPE) { Activate(Close, true); return true; }
        if (key == VK_TAB) { TabTo(shift); return true; }
        const bool typing = flow_.step() == Step::Write && (focus_ == Field || focus_ == None);
        if (typing && key == VK_BACK) { Erase(); return true; }
        if (typing && ctrl && key == 'V') { Paste(); return true; }
        if (key == VK_RETURN) {
            if (typing) { if (flow_.CanSend()) Activate(Send, true); return true; }
            if (focus_ == None) Activate(Primary, true); else Activate(focus_, true); return true;
        }
        if (key == VK_SPACE && focus_ != None && !typing) { Activate(focus_, true); return true; }
        if ((key == VK_DOWN || key == VK_F4) && focus_ == Lang) { Activate(Lang, true); return true; }
        if ((key == VK_LEFT || key == VK_RIGHT) && (focus_ == ClaudeRow || focus_ == CodexRow)) { Focus(focus_ == ClaudeRow ? CodexRow : ClaudeRow, true); return true; }   // A6: between the tiles
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
    void DrawButtons(Step s, float alpha) {
        const auto r = Rect(Secondary);
        if (r.right > r.left) P::FooterButton(*c_, r, SecondaryLabel(s), K::Token::raised, false, 500, glow_[Secondary], pressed_ == Secondary && hover_ == Secondary ? 1.f : 0.f, true, focus_ == Secondary && keyboard_, alpha);
        const auto p = Rect(Primary); if (p.right <= p.left) return;
        if (TextPrimary(s)) {   // « Passer l'essai »: 14/20 500 #FFFFFFB3, white under the pointer (Val's default #7)
            const float lift = std::max(glow_[Primary], pressed_ == Primary && hover_ == Primary ? 1.f : 0.f);
            K::Text(*c_, PrimaryLabel(s), {p.left, p.top + 6, p.right, p.bottom}, {14, 20, 500, 0}, PopupKit::White((0xB3 / 255.f + lift * (1 - 0xB3 / 255.f)) * alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
            if (focus_ == Primary && keyboard_) PopupKit::InsetRing(*c_, p, P::ButtonRadius);
        } else if (PrimaryDimmed(s)) {   // A6 « Installation… » (Paper 231S-0): #1F4A4C, #FFFFFF99 — the profile's hue
            K::Fill(*c_, p, P::ButtonRadius, K::Col(Tone(0x1F4A4C), alpha));
            K::Text(*c_, PrimaryLabel(s), {p.left, p.top + 6, p.right, p.bottom}, {14, 20, 500, 0}, PopupKit::White((0x99 / 255.f) * alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
            if (focus_ == Primary && keyboard_) PopupKit::InsetRing(*c_, p, P::ButtonRadius);
        } else P::FooterButton(*c_, p, PrimaryLabel(s), AccentHex(), true, 500, glow_[Primary], pressed_ == Primary && hover_ == Primary ? 1.f : 0.f, true, focus_ == Primary && keyboard_, alpha);
    }
    void DrawDots(Step s, float alpha) {
        if (Alone()) return;   // « Ton agent » alone: no tutorial, no progress
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
    // The pointer of the mode (board 6's crosshair, 32): white 4 under dark 1.6, the accent dot ringed in white.
    void DrawCrosshair(D2D1_POINT_2F at, float alpha) {
        auto* t = c_->RenderTarget(); auto* b = c_->SolidBrush(); if (!t || !b) return;
        const D2D1_RECT_F cross = Box(at.x - 16, at.y - 16, 32, 32);
        glyphs_.Stroke(*c_, K::Path::cross, cross, 32, 4, K::Col(K::Token::text, alpha)); glyphs_.Stroke(*c_, K::Path::cross, cross, 32, 1.6f, K::Col(0x1C1C1C, alpha));
        b->SetColor(K::Col(Accent(), alpha)); t->FillEllipse(D2D1::Ellipse(at, 2.6f, 2.6f), b);
        b->SetColor(K::Col(K::Token::text, alpha)); t->DrawEllipse(D2D1::Ellipse(at, 2.6f, 2.6f), b, 1.2f);
    }
    // The fake page at `left` (its own content: no frame, no pointer).
    void DrawSite(float left, float payW, float alpha) {
        auto* t = c_->RenderTarget(); if (!t || alpha <= 0) return;
        const D2D1_RECT_F site = SiteBox(left);
        K::Fill(*c_, site, Layout::siteRadius, K::Col(0xF4F3F1, alpha));
        ID2D1Layer* layer = nullptr; ID2D1RoundedRectangleGeometry* clip = nullptr;
        if (c_->Factory() && SUCCEEDED(c_->Factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(site, Layout::siteRadius, Layout::siteRadius), &clip)) && SUCCEEDED(t->CreateLayer(&layer)))
            t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip), layer);
        const D2D1_RECT_F header = Box(left, Layout::siteTop, Layout::siteW, Layout::siteHeader);
        K::Fill(*c_, header, 0, K::Col(0xFFFFFF, alpha)); K::Fill(*c_, {header.left, header.bottom - 1, header.right, header.bottom}, 0, D2D1::ColorF(0, 0, 0, (0x12 / 255.f) * alpha));
        K::Text(*c_, TR(annotation_onboarding_site_name), {left + 16, Layout::siteTop + 10, left + 260, Layout::siteTop + 26}, {13, 16, 600, 0}, K::Col(0x1C1C1C, alpha));
        K::Text(*c_, TR(annotation_onboarding_site_sample), {left + 160, Layout::siteTop + 11, left + Layout::siteW - 16, Layout::siteTop + 25}, {11, 14, 400, 0}, D2D1::ColorF(0, 0, 0, (0x73 / 255.f) * alpha), DWRITE_TEXT_ALIGNMENT_TRAILING);
        const auto title = PartBox(Part::Title, left, payW);
        K::Text(*c_, TR(annotation_onboarding_site_pay_title), {title.left, title.top, title.left + 300, title.bottom}, {18, 22, 600, -.36f}, K::Col(0x1C1C1C, alpha));
        const std::pair<Part, std::wstring> fields[] = {{Part::Email, TR(annotation_onboarding_site_email)}, {Part::Card, L"1234 1234 1234 1234"}};   // i18n-exempt: a card number placeholder
        for (const auto& f : fields) {
            const auto r = PartBox(f.first, left, payW);
            K::Fill(*c_, r, 6, K::Col(0xFFFFFF, alpha)); K::Border(*c_, r, 6, 1, D2D1::ColorF(0, 0, 0, (0x14 / 255.f) * alpha));
            K::Text(*c_, f.second, {r.left + 10, r.top + 7, r.right - 10, r.bottom}, {12, 16, 400, 0}, D2D1::ColorF(0, 0, 0, (0x4D / 255.f) * alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        }
        const auto pay = PartBox(Part::Pay, left, payW);
        K::Fill(*c_, pay, 8, K::Col(0x1C1C1C, alpha));
        K::Text(*c_, TR(onboarding_demo_pay), {pay.left - 40, pay.top + 10, pay.right + 40, pay.bottom}, {13, 16, 600, 0}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (layer) { t->PopLayer(); layer->Release(); }
        if (clip) clip->Release();
        if (focus_ == Payer && keyboard_ && flow_.step() == Step::Pick) PopupKit::InsetRing(*c_, Grow(pay, 2), 9);
    }
    // The selection frame of the mode with its tag above it (A2: radius 2; A3: square).
    void DrawFrame(D2D1_RECT_F element, const std::wstring& tag, float radius, float alpha) {
        const auto accent = Accent(); const D2D1_RECT_F frame = Grow(element, Layout::frameOut);
        K::Fill(*c_, frame, radius, K::Col(accent, (0x14 / 255.f) * alpha)); K::Border(*c_, frame, radius, 2, K::Col(accent, alpha));
        const K::Font f{12, 16, 400, 0}; const float w = K::Width(*c_, tag, f) + 12, top = frame.top - Layout::tagH;
        K::Fill(*c_, Box(frame.left, top, w, Layout::tagH), 3, K::Col(accent, alpha)); K::Fill(*c_, Box(frame.left, top + 9, w, 9), 0, K::Col(accent, alpha));   // radius 3 on the top corners only
        K::Text(*c_, tag, {frame.left + 6, top + 1, frame.left + w + 8, top + 17}, f, PopupKit::White(.9f * alpha));
    }
    // A pill of the trial (h 36, radius 18, #171717, padding 8 / 12, gap 8): a 20 disc in the accent, then the label.
    void DrawPill(float left, const std::wstring& label, bool check, const std::wstring& number, float alpha) {
        const K::Font f{13, 16, 500, 0}; const float w = 8 + 20 + 8 + K::Width(*c_, label, f) + 12, top = Layout::pillTop;
        K::Fill(*c_, Box(left, top, w, Layout::pillH), 18, K::Col(K::Token::canvas, alpha));
        const D2D1_RECT_F disc = Box(left + 8, top + 8, 20, 20);
        K::Fill(*c_, disc, 10, K::Col(Accent(), alpha));
        if (check) glyphs_.Fill(*c_, A::Glyphs::check.d, Box(disc.left + 4, disc.top + 4, 12, 12), A::Glyphs::check.w, K::Col(K::Token::text, alpha), A::Glyphs::check.x, A::Glyphs::check.y);
        else K::Text(*c_, number, {disc.left, disc.top + 4, disc.right, disc.bottom}, {11, 12, 600, 0}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        K::Text(*c_, label, {left + 36, top + 10, left + w, top + 26}, f, K::Col(K::Token::text, alpha));
    }
    // What the trial's steps draw over the page (the page itself may be drawn by the transition).
    void DrawTrial(Step s, Motion::Time now, float alpha, bool withSite) {
        const float left = SiteLeft(s), payW = PayWidth(s, now);
        if (withSite) DrawSite(left, payW, alpha);
        if (s == Step::Pick) {
            if (part_ != Part::None) { const auto r = PartBox(part_, left, payW); DrawFrame(r, AH_Wide(AO::TagOf(part_)), 2, alpha); }
            DrawPill(Layout::coachLeft, TR(annotation_onboarding_coach), false, L"1", alpha);   // i18n-exempt: the step's number
            if (In(SiteBox(left), pointer_.x, pointer_.y)) DrawCrosshair(pointer_, alpha);
        } else if (s == Step::Write) {
            DrawFrame(PartBox(Part::Pay, left, payW), L"button.btn-primary", 0, alpha);   // i18n-exempt: a CSS selector in the trial
            const float textH = NoteTextHeight();
            P::DemoNote(*c_, glyphs_, Layout::noteLeft, Layout::noteTop, textH, Accent(), alpha, flow_.CanSend() ? 1.f : K::Token::disabled);
            const auto caret = K::Col(Tone(0x0AF4FF), (caretOn_ ? 1.f : 0.f) * alpha);
            std::vector<K::Span> runs;
            if (flow_.text().empty()) { runs.push_back({L"|", caret}); runs.push_back({TR(annotation_onboarding_write_hint), PopupKit::White(.6f * alpha)}); }
            else { runs.push_back({flow_.text(), PopupKit::White(.9f * alpha)}); runs.push_back({L"|", caret}); }
            const auto field = NoteField();
            K::Spans(*c_, runs, {field.left, field.top, field.right, field.top + textH}, {14, 20, 400, 0}, DWRITE_TEXT_ALIGNMENT_LEADING, true);
            if (focus_ == Send && keyboard_) PopupKit::InsetRing(*c_, Rect(Send), 8);
        } else if (s == Step::Result) {
            const float done = DoneShown(now) * alpha; if (done <= 0) return;
            const auto pay = PartBox(Part::Pay, left, AO::PayTo);
            const K::Font f{11, 14, 600, 0}; const std::wstring measure = L"160 → 260"; const float mw = K::Width(*c_, measure, f), mx = pay.left + (AO::PayTo - mw) / 2, my = pay.bottom + 6;   // i18n-exempt: the widths of the trial
            const auto accent = K::Col(Accent(), done);
            K::Fill(*c_, {pay.left, my + 7.25f, mx - 6, my + 8.75f}, 0, accent); K::Fill(*c_, {mx + mw + 6, my + 7.25f, pay.right, my + 8.75f}, 0, accent);
            K::Text(*c_, measure, {mx, my + 1, mx + mw + 4, my + 15}, f, K::Col(Tone(0x007D83), done));
            DrawPill(pay.right + Layout::doneGap, TR(annotation_onboarding_done), true, L"", done);
        }
    }
    static std::wstring AH_Wide(const char* s) { std::wstring w; for (const char* p = s; *p; ++p) w += static_cast<wchar_t>(*p); return w; }
    // A5: a local project, a local file, a site on the web (button off and why).
    void DrawWhere(float alpha) {
        const K::Font url{16, 20, 400, -.16f}, pick{13, 16, 500, 0}, tip{13, 18, 400, 0};
        const std::wstring annotate = TR(a11y_annotate); const float pw = Layout::pickPad + 14 + 6 + K::Width(*c_, annotate, pick) + Layout::pickPad;
        auto row = [&](float top, float fillAlpha, const std::vector<K::Span>& text, bool on) {
            const D2D1_RECT_F r = Box(Layout::rowsLeft, top, Layout::rowsW, Layout::rowH);
            K::Fill(*c_, r, Layout::rowRadius, K::Col(K::Token::canvas, fillAlpha * alpha));
            const D2D1_RECT_F b = Box(r.right - Layout::rowPadR - pw, top + (Layout::rowH - Layout::pickH) / 2, pw, Layout::pickH);
            K::Spans(*c_, text, {r.left + Layout::rowPadL, top + 14, b.left - 10, top + 34}, url, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
            K::Fill(*c_, b, Layout::pickRadius, on ? K::Col(Accent(), alpha) : PopupKit::White((0x0F / 255.f) * alpha));
            const D2D1_COLOR_F ink = on ? K::Col(K::Token::text, alpha) : PopupKit::White((0x52 / 255.f) * alpha);
            glyphs_.Stroke(*c_, K::Path::cross, Box(b.left + Layout::pickPad, b.top + 9, 14, 14), 32, 3, ink);
            K::Text(*c_, annotate, {b.left + Layout::pickPad + 20, b.top + 8, b.right, b.top + 24}, pick, ink);
        };
        const float step = Layout::rowH + Layout::rowsGap;
        row(Layout::rowsTop, 1, {{L"localhost", K::Col(K::Token::text, alpha)}, {L":5173", PopupKit::White(.5f * alpha)}}, true);   // i18n-exempt: an address in the illustration
        row(Layout::rowsTop + step, 1, {{TR(annotation_onboarding_file), PopupKit::White(.5f * alpha)}, {L"  …/boutique/", PopupKit::White(.5f * alpha)}, {L"index.html", K::Col(K::Token::text, alpha)}}, true);   // i18n-exempt: a path in the illustration
        row(Layout::rowsTop + 2 * step, 0xE6 / 255.f, {{L"lemonde.fr", PopupKit::White(.6f * alpha)}, {L"/planete", PopupKit::White((0x4D / 255.f) * alpha)}}, false);   // i18n-exempt: an address in the illustration
        const std::wstring t1 = TR(annotation_onboarding_local_only), t2 = TR(annotation_onboarding_local_hint);
        const float w = std::min(Layout::tipMax, std::max(K::Width(*c_, t1, tip), K::Width(*c_, t2, tip)) + 2 * 10 + 2), top = Layout::rowsTop + 3 * step;
        const D2D1_RECT_F r = Box(Layout::rowsLeft + Layout::rowsW - w, top, w, 1 + 6 + 18 + 2 + 18 + 6 + 1);
        K::Fill(*c_, r, 8, K::Col(K::Token::canvas, alpha)); K::Border(*c_, r, 8, 1, PopupKit::White((0x14 / 255.f) * alpha));
        K::Text(*c_, t1, {r.left + 11, r.top + 7, r.right - 11, r.top + 25}, tip, PopupKit::White((0xE6 / 255.f) * alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        K::Text(*c_, t2, {r.left + 11, r.top + 27, r.right - 11, r.top + 45}, tip, PopupKit::White(.5f * alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
    }
    void Logo(Agent a, D2D1_RECT_F box, float alpha) {
        const auto& g = a == Agent::Claude ? A::Glyphs::claude : A::Glyphs::openai;
        glyphs_.Fill(*c_, g.d, box, g.w, a == Agent::Claude ? K::Col(0xD97757, alpha) : K::Col(K::Token::text, alpha), g.x, g.y);
    }
    D2D1_COLOR_F InkColour(TonAgent::Ink ink, float alpha) const {
        return ink == TonAgent::Ink::Danger ? K::Col(K::Token::danger, alpha) : ink == TonAgent::Ink::Accent ? K::Col(Tone(0x2FD1D9), alpha) : K::Col(0xA6A6A6, alpha);
    }
    // The mark under a tile's name, centred in the tile at `top` (Paper: icon 14, gap 4, 13/13 500; « Erreur » 13/17 with
    // its icon centred; « ✓ Prêt » one text 13/17). « Installation… » plays the Downloads morph; its end plays too.
    void DrawMark(Agent a, D2D1_RECT_F tile, float top, bool target, Motion::Time now, float alpha) {
        const int i = TonAgent::Index(a); const auto& s = St(a); const TonAgent::Mark m = TonAgent::MarkOf(s);
        if (m == TonAgent::Mark::Unknown) return;
        const auto colour = InkColour(TonAgent::InkOf(s, target), alpha); const float cx = (tile.left + tile.right) / 2;
        const bool animate = !options_.reduced && !offscreen_; const bool ending = animate && Ending(i, now);
        const float endMs = ending ? Ms(ended_[i], now) : 0;
        const bool morphing = ending && endKind_[i] == 1 && installGlyph_.Success();   // success: the arrow morphs into the check (then the mark)
        if (m == TonAgent::Mark::Ready && !morphing) {
            const std::wstring text = std::wstring(L"\u2713 ") + MarkWord(s); const K::Font f{13, 17, 500, 0}; const float w = K::Width(*c_, text, f);   // i18n-exempt: the check sign
            K::Text(*c_, text, {std::round(cx - w / 2), top, std::round(cx - w / 2) + w + 2, top + 17}, f, colour);
            return;
        }
        const std::wstring word = MarkWord(s); const bool tall = m == TonAgent::Mark::Error;
        const K::Font f{13, tall ? 17.f : 13.f, 500, 0}; const float w = K::Width(*c_, word, f), total = TonAgent::Card::markIcon + TonAgent::Card::markGap + w;
        const float x = std::round(cx - total / 2), iconTop = top + (tall ? 2.f : 0.f), textTop = top + (tall ? 0.f : .5f);   // « Erreur »: its icon centred in 17 (1.5, on the next pixel as drawn)
        const D2D1_RECT_F icon = Box(x, iconTop, 14, 14);
        auto still = [&](const char* d, D2D1_COLOR_F col) { glyphs_.Fill(*c_, d, icon, TonGlyph::side, col); };
        if (morphing) {
            DownloadIcon::Pose pose; pose.idle = false; pose.ring = DownloadIcon::FillEnd; pose.arrow = DownloadIcon::Morph; pose.success = std::min(DownloadIcon::SuccessMorph, static_cast<int>(endMs * 60 / 1000));
            if (!installGlyph_.Draw(c_->RenderTarget(), i, icon, Scale(), pose, Tone(0x2FD1D9), alpha)) still(TonGlyph::download, colour);
        } else if (m == TonAgent::Mark::Installing && animate) {
            const auto pose = install_[i].At(now);
            if (pose.idle || !installGlyph_.Draw(c_->RenderTarget(), i, icon, Scale(), pose, Tone(0x2FD1D9), alpha)) still(TonGlyph::download, colour);
        } else if (m == TonAgent::Mark::Error && ending && endKind_[i] == 2) {   // the install failed: the icon fades into the error icon
            const float u = Motion::Clamp(endMs / FailMs);
            still(TonGlyph::download, K::Col(Tone(0x2FD1D9), (1 - u) * alpha)); still(TonGlyph::error, InkColour(TonAgent::Ink::Danger, u * alpha));
        } else still(m == TonAgent::Mark::Connect ? TonGlyph::wifi : m == TonAgent::Mark::Error ? TonGlyph::error : TonGlyph::download, colour);
        K::Text(*c_, word, {x + 18, textTop, x + 18 + w + 2, textTop + f.line}, f, colour);
    }
    // A tile (Paper 2351-0 at rest, 235O-0 current, 236S-0 failed): the frame, the logo, the name, the mark.
    void DrawTile(Agent a, D2D1_RECT_F r, bool target, Motion::Time now, float alpha) {
        namespace C = TonAgent::Card; const int id = a == Agent::Claude ? ClaudeRow : CodexRow; const auto look = TonAgent::LookOf(St(a), target);
        if (look == TonAgent::Look::Current) { K::Fill(*c_, r, C::tileRadius, K::Col(Tone(0x192A2A), alpha)); K::Border(*c_, r, C::tileRadius, 1.5f, K::Col(Tone(0x2FD1D9), alpha)); }
        else if (look == TonAgent::Look::Danger) { K::Fill(*c_, r, C::tileRadius, K::Col(K::Token::danger, K::Token::focus * alpha)); K::Border(*c_, r, C::tileRadius, 1.5f, K::Col(K::Token::danger, alpha)); }
        else {
            const float lift = std::max(glow_[id], pressed_ == id && hover_ == id ? 1.f : 0.f);   // hover: a step lighter
            K::Fill(*c_, r, C::tileRadius, K::Col(K::Lerp(K::FromHex(0x1F1F1F), K::FromHex(0x262626), lift), alpha)); K::Border(*c_, r, C::tileRadius, 1, K::Col(0x2A2A2A, alpha));
        }
        const float cx = (r.left + r.right) / 2;
        {   // the logo in its 34 box, clipped to it like the board's <svg> (Claude Code's top rays leave its view box)
            const D2D1_RECT_F box = Box(std::floor(cx - C::logo / 2), r.top + C::tilePadTop, C::logo, C::logo);
            if (auto* t = c_->RenderTarget()) { t->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED); Logo(a, box, alpha); t->PopAxisAlignedClip(); }
        }
        K::Text(*c_, Name(a), {r.left, r.top + C::tilePadTop + C::logo + 10, r.right, r.top + C::tilePadTop + C::logo + 10 + C::nameH}, {16, 21, 600, 0}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        DrawMark(a, r, r.top + C::markTop, target, now, alpha);
        if (focus_ == id && keyboard_) PopupKit::InsetRing(*c_, r, C::tileRadius);
    }
    // A6 « Ton agent » (Paper 22ZR-0 … 251H-0).
    void DrawTon(Motion::Time now, float alpha) {
        namespace C = TonAgent::Card;
        const auto k = TonLayout(std::floor(CardTopAt(now))); const Agent target = Target();   // on a whole pixel, as the boards draw it (141.5 → 141)
        K::Fill(*c_, k.card, C::radius, K::Col(K::Token::canvas, alpha));
        DrawTile(Agent::Claude, k.tile[0], target == Agent::Claude, now, alpha);
        DrawTile(Agent::Codex, k.tile[1], target == Agent::Codex, now, alpha);
        if (k.kind == TonAgent::Trouble::None) return;
        K::Text(*c_, TroubleText(), k.trouble, {12, 16, 400, 0}, K::Col(0xA6A6A6, alpha), DWRITE_TEXT_ALIGNMENT_CENTER, true);
        if (k.kind != TonAgent::Trouble::Install) return;
        // « Installer moi-même ↗ » 12/16 500 #E6E6E6, underlined from the font (Paper 2378-0); hover: white.
        const std::wstring manual = TR(ton_agent_manual);
        if (auto* layout = K::MakeLayout(*c_, manual, k.manual.right - k.manual.left + 4, {12, 16, 500, 0}, false, DWRITE_TEXT_ALIGNMENT_LEADING, false)) {
            layout->SetUnderline(TRUE, {0, static_cast<UINT32>(manual.size())});
            if (auto* b = c_->SolidBrush()) { b->SetColor(PopupKit::White((0xE6 / 255.f + glow_[Manual] * (1 - 0xE6 / 255.f)) * alpha)); c_->RenderTarget()->DrawTextLayout(D2D1::Point2F(k.manual.left, k.manual.top), layout, b, D2D1_DRAW_TEXT_OPTIONS_NONE); }
            layout->Release();
        }
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
        auto join = [&] { K::Text(*c_, L"+", {x, top + (Layout::keyH - 24) / 2, x + wp + 2, top + (Layout::keyH + 24) / 2}, plus, K::Col(Tone(0x0B3B3D), alpha)); x += wp + Layout::keyGap; };   // i18n-exempt: a symbol
        cap(ctrl, wc); join(); cap(shift, ws); join(); cap(letter, Layout::keyA);
        const auto r = Reader(); const K::Font f{13, 16, 500, 0};
        const std::wstring status = r == AO::Reader::None ? std::wstring(TR(ton_agent_none_ready)) : TRF(ton_agent_ready_title, AO::ReaderName(r));   // the agents' one status
        const float pw = Layout::statusPad + 8 + 8 + K::Width(*c_, status, f) + Layout::statusPad, py = top + Layout::keyH + Layout::pillGap;
        const D2D1_RECT_F pill = Box(std::round(Layout::width / 2 - pw / 2), py, pw, Layout::statusH);
        K::Fill(*c_, pill, 16, K::Col(K::Token::canvas, alpha));
        K::Fill(*c_, Box(pill.left + Layout::statusPad, py + 12, 8, 8), 4, r == AO::Reader::None ? PopupKit::White(.3f * alpha) : K::Col(Tone(0x2FD1D9), alpha));
        K::Text(*c_, status, {pill.left + Layout::statusPad + 16, py + 8, pill.right, py + 24}, f, PopupKit::White((0xE6 / 255.f) * alpha));
    }
    void DrawContent(Step s, Motion::Time now, float alpha, bool siteDrawn) {
        if (alpha <= 0 || s == Step::Closed) return;
        switch (s) {
        case Step::What: P::AnnotationDemo(*c_, glyphs_, Accent(), alpha); break;
        case Step::Pick: case Step::Write: case Step::Result: DrawTrial(s, now, alpha, !siteDrawn); break;
        case Step::Where: DrawWhere(alpha); break;
        case Step::Connect: DrawTon(now, alpha); break;
        case Step::Ready: DrawReady(alpha); break;
        default: break;
        }
        DrawText(s, alpha); DrawDots(s, alpha); DrawButtons(s, alpha);
    }
    void Draw(Motion::Time now) {
        auto* t = c_->RenderTarget(); if (!t) return;
        P::TextLook(*c_, t);
        EnsureGpu();
        if (gpu_.Ready()) P::Shade(gpu_, grain_, t, Onboarding::Shaders::Kind::GrainGradient, P::GrainUniforms(ShaderSeconds(now), Tint()), P::GrainW, P::GrainH, ShadersLoop(), Due(now), static_cast<float>(Tint().hue), now);
        trame_.Shade(t, ShaderSeconds(now), Onboarding::Tuning::Defaults(Onboarding::Tuning::Trame), now);
        t->Clear(K::Col(0x000000));
        P::GrainBackdrop(*c_, grain_, trame_, Tint(), 1);
        K::Gradient(*c_, {-2, P::GrainVeilTop, 727, 579}, 0, {0, P::GrainVeilTop}, {0, 579}, {{P::GrainVeilFrom, K::FromHex(0x000000), 0}, {P::GrainVeilTo, K::FromHex(0x000000), 1}});
        const Step live = arranged_;
        const float raw = Change(now); const bool swapped = !changing_ || raw >= .4f; const Step content = swapped ? shown_ : from_;
        const float alpha = !changing_ ? 1.f : raw < .4f ? 1 - raw / .4f : (raw - .4f) / .6f;
        // Between two steps of the trial the page stays and glides to its place (A3 sits 40 to the left).
        const bool glide = changing_ && AO::Trial(from_) && AO::Trial(shown_);
        if (glide) { const float u = EaseOut(raw), left = SiteLeft(from_) + (SiteLeft(shown_) - SiteLeft(from_)) * u; DrawSite(left, content == Step::Result ? PayWidth(Step::Result, now) : AO::PayFrom, 1); }
        if (content != arranged_) Arrange(content);
        auto control = [&](int id, D2D1_RECT_F r) { return P::Control{r, .08f * glow_[id] + (pressed_ == id && hover_ == id ? .04f : 0) + (id == Lang && menuOpen_ ? .08f : 0), focus_ == id && keyboard_}; };
        P::HeaderControls(*c_, glyphs_, control(Lang, LangBox()), control(Close, CloseBox()), 1, 1);
        DrawContent(content, now, alpha, glide);
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
            if (s == Step::Pick) { const auto i = button(Payer, TR(onboarding_demo_pay), L"AnnotationOnboardingPay"); nodes[i].help = TR(annotation_onboarding_coach); }
            if (s == Step::Write) {
                Accessibility::Node e; e.key = Field; e.parent = 500; e.role = Accessibility::Role::Edit; e.name = TR(annotation_onboarding_write_title); e.value = flow_.text(); e.help = TR(annotation_onboarding_write_hint); e.automationId = L"AnnotationOnboardingNote"; e.bounds = bounds(Rect(Field)); nodes.push_back(e);
                button(Send, TR(annotation_onboarding_send), L"AnnotationOnboardingSend");
            }
            if (s == Step::Result) text(603, TR(annotation_onboarding_done));
            if (s == Step::Where) { text(600, TR(annotation_onboarding_local_only)); text(601, TR(annotation_onboarding_local_hint)); }
            if (s == Step::Connect) {
                for (Agent a : AgentStatus::AllAgents) {
                    const int id = a == Agent::Claude ? ClaudeRow : CodexRow; const auto i = button(id, Name(a), a == Agent::Claude ? L"AnnotationOnboardingClaude" : L"AnnotationOnboardingCodex");
                    nodes[i].status = MarkWord(St(a)); nodes[i].help = TRF(ton_agent_tile_help, Name(a));
                }
                if (TonAgent::TroubleOf(TargetStatus()) != TonAgent::Trouble::None) { std::wstring t = TroubleText(); std::replace(t.begin(), t.end(), L'\n', L' '); text(604, t); }
                if (Enabled(Manual)) button(Manual, TR(ton_agent_manual), L"AnnotationOnboardingManual");
            }
            if (s == Step::Ready) { const auto r = Reader(); text(602, r == AO::Reader::None ? std::wstring(TR(ton_agent_none_ready)) : TRF(ton_agent_ready_title, AO::ReaderName(r))); }
            button(Secondary, SecondaryLabel(s), L"AnnotationOnboardingSecondary");
            const auto primary = button(Primary, PrimaryLabel(s), L"AnnotationOnboardingPrimary"); if (!TextPrimary(s) && Enabled(Primary)) nodes[primary].accelerator = TR(key_enter);
            const auto lang = button(Lang, TR(language_label), L"AnnotationOnboardingLanguage"); nodes[lang].value = UiLanguage::Autonym(I18n::Current()); nodes[lang].expand = 1;
            const auto close = button(Close, TR(common_close), L"AnnotationOnboardingClose"); nodes[close].accelerator = TR(key_escape);
            return nodes;
        };
        host.focused = [this]() -> long long { return focus_; };
        host.invoke = [this](long long key) { Activate(static_cast<int>(key), true); };
        host.expand = [this](long long key, bool open) { if (open) Activate(static_cast<int>(key), true); };
        host.focus = [this](long long key) { Focus(static_cast<int>(key), true); };
        host.setValue = [this](long long key, const std::wstring& v) { if (key == Field && flow_.step() == Step::Write) { flow_.SetText(std::wstring()); Type(v); } };
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
    void Pointer(float x, float y) {   // A2: the element under the pointer, the crosshair with it
        const D2D1_POINT_2F was = pointer_; pointer_ = {x, y};
        if (flow_.step() != Step::Pick) return;
        const Part p = PartAt(x, y);
        if (p != part_ || In(SiteBox(Layout::siteLeft), x, y) || In(SiteBox(Layout::siteLeft), was.x, was.y)) { part_ = p; Invalidate(); }
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = Scale();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case FrameMessage: if (l == FrameClock::ClockLost) { clock_.Stop(); clockOk_ = false; SetTimer(h, 1, 16, nullptr); return 0; } clock_.Acknowledge(); Step_(); return 0;
        case WM_TIMER: if (w == 1) Step_(); if (w == CaretTimer) { caretOn_ = !caretOn_; Invalidate(); } return 0;
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p);
            if (p.y / s < 48 && Hit(p.x / s, p.y / s) == None) return HTCAPTION;   // the header moves the window
            return HTCLIENT;
        }
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT && reinterpret_cast<HWND>(w) == h) {
                POINT p{}; GetCursorPos(&p); ScreenToClient(h, &p); const float x = p.x / s, y = p.y / s;
                if (flow_.step() == Step::Pick && In(SiteBox(Layout::siteLeft), x, y)) { SetCursor(nullptr); return TRUE; }   // the mode's crosshair is drawn instead
                if (flow_.step() == Step::Write && In(Rect(Field), x, y)) { SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE; }
            }
            break;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr);
            const float x = GET_X_LPARAM(l) / s, y = GET_Y_LPARAM(l) / s; Pointer(x, y);
            const int id = Hit(x, y);
            if (id != hover_) { hover_ = id; if (id) glow_[id]; Invalidate(); RequestFrame(); }
            return 0;
        }
        case WM_MOUSELEAVE: pointer_ = {-100, -100}; if (part_ != Part::None) { part_ = Part::None; } if (hover_) hover_ = None; Invalidate(); RequestFrame(); return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: { const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); keyboard_ = false; SetFocus(h); SetCapture(h); pressed_ = id; if (id) focus_ = id; Invalidate(); return 0; }
        case WM_LBUTTONUP: {
            const int was = pressed_; pressed_ = None; if (GetCapture() == h) ReleaseCapture();
            const int id = Hit(GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s); Invalidate();
            if (id && id == was) Activate(id, false);
            return 0;
        }
        case WM_CAPTURECHANGED: pressed_ = None; Invalidate(); return 0;
        case WM_KEYDOWN: Key(w); return 0;
        case WM_CHAR: if (flow_.step() == Step::Write && (focus_ == Field || focus_ == None) && w >= 0x20 && !(GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0)) { const wchar_t ch = static_cast<wchar_t>(w); Type(std::wstring(1, ch)); } return 0;
        case WM_SYSKEYDOWN: if (w == VK_F4) { Activate(Close, true); return 0; } break;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE && open_) { SetFocus(h); if (flow_.step() == Step::Connect) AgentStatus::Refresh(); } return 0;   // A6: back from the web page or PowerShell
        case WM_DPICHANGED: { dpi_ = HIWORD(w); const RECT* r = reinterpret_cast<const RECT*>(l); SetWindowPos(h, nullptr, r->left, r->top, Px(Layout::width), Px(Layout::height), SWP_NOZORDER | SWP_NOACTIVATE); NativeUI::ConfigureShadowlessPopup(h, Layout::radius); chrome_.Resize(); grain_.Drop(); trame_.Drop(); Arrange(); Invalidate(); return 0; }
        case WM_CLOSE: Activate(Close, false); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    // A new opening: no tile picked, nothing moving; the statuses as they are now, followed from here on.
    void ResetTon() {
        pick_.Reset(); chatAgent_ = Agent::Claude; manualForTest_ = 0; page_.clear(); cardFrom_ = cardTo_ = -1;
        for (int i = 0; i < 2; ++i) { install_[i] = DownloadIcon::Model{}; endKind_[i] = 0; endTo_[i] = AState::Unknown; }
        Listen();
        for (Agent a : AgentStatus::AllAgents) if (St(a).state == AState::Installing) install_[TonAgent::Index(a)].Update(true, 0, false, Motion::Now(), options_.reduced || offscreen_);   // already installing (opened again)
    }
public:
    Window() = default; Window(const Window&) = delete; Window& operator=(const Window&) = delete;
    ~Window() { if (open_) { silent_ = true; Finish(); } Unlisten(); }
    HWND Handle() const { return hwnd_; }
    bool Visible() const { return open_; }
    const AO::Flow& Flow() const { return flow_; }
    // Opens over the browser: `owner` its window, `ownerClient` its client area in screen pixels (centring, veil).
    // `root`: the folder that holds assets\. False: nothing shown (the host falls back to the « Connecter un agent » modal).
    bool Open(HINSTANCE instance, HWND owner, const std::wstring& root, RECT ownerClient, Options options) {
        if (open_) return true;
        instance_ = instance; owner_ = owner; root_ = root; options_ = std::move(options); claude_ = options_.claude; codex_ = options_.codex;
        flow_ = AO::Flow(options_.start, options_.alone); manual_ = silent_ = false; offscreen_ = false; focus_ = hover_ = pressed_ = None; keyboard_ = false; glow_.clear(); part_ = Part::None;
        ResetTon(); installGlyph_.SetRoot(root_);
        if (!PopupKit::Register(instance, L"BrowserAnnotationOnboarding", Proc) || !PopupKit::Register(instance, L"BrowserAnnotationOnboardingVeil", VeilProc)) return false;
        veil_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"BrowserAnnotationOnboardingVeil", L"", WS_POPUP, ownerClient.left, ownerClient.top, ownerClient.right - ownerClient.left, ownerClient.bottom - ownerClient.top, owner, nullptr, instance, nullptr);
        if (veil_) SetLayeredWindowAttributes(veil_, 0, 0, LWA_ALPHA);
        hwnd_ = PopupKit::CreatePopup(instance, owner, L"BrowserAnnotationOnboarding", Title(flow_.step()).c_str(), 0, this, Layout::radius);
        if (!hwnd_) { if (veil_) { DestroyWindow(veil_); veil_ = nullptr; } return false; }
        dpi_ = owner ? GetDpiForWindow(owner) : 96; if (!dpi_) dpi_ = 96;
        if (FAILED(chrome_.Init(hwnd_, root_))) { Dispose(); return false; }
        Place(ownerClient); dpi_ = GetDpiForWindow(hwnd_) ? GetDpiForWindow(hwnd_) : dpi_; Place(ownerClient);
        menu_.Init(instance, hwnd_, root_);
        clockOk_ = clock_.Start(hwnd_, FrameMessage);
        open_ = true; shown_ = from_ = flow_.step(); changing_ = leaving_ = false; Arrange(); AttachUia();
        Paint();   // the first frame builds the shader: before the window shows
        opened_ = Motion::Now(); lastStep_ = {}; entering_ = true;
        if (veil_) ShowWindow(veil_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        if (owner_) EnableWindow(owner_, FALSE);   // modal: the browser behind waits, as for « Connecter un agent »
        SetForegroundWindow(hwnd_); SetFocus(hwnd_); RequestFrame();
        if (uia_.Attached()) uia_.Raise(500, UIA_Window_WindowOpenedEventId);
        Announce(Title(flow_.step()) + L". " + Body(flow_.step()));
        if (flow_.step() == Step::Connect) AgentStatus::Refresh();   // A6 shown: the agents detected again
        return true;
    }
    // Brought back to the front (the band's reminder or the chat asked again while it is open).
    void Raise() { if (!open_ || !hwnd_ || offscreen_) return; SetForegroundWindow(hwnd_); SetFocus(hwnd_); if (flow_.step() == Step::Connect) AgentStatus::Refresh(); }
    bool AloneOpen() const { return open_ && flow_.alone(); }
    // The mode ended under it (the page went away): it goes at once and the host is not told (nothing was answered).
    void Dismiss() { if (!open_) return; silent_ = true; leaving_ = false; Finish(); }
    // (before « Ton agent ») the host's MCP connection status for A6's rows: kept as given, never shown — A6 and A7 read
    // the agents' one status (src/agent-status.h). Left for the host's code (src/annotation-mode.h, worker C).
    void SetStatus(A::AgentKind a, const A::AppStatus& status) { if (a == A::AgentKind::Other) return; (a == A::AgentKind::Claude ? claude_ : codex_) = status; }
    const A::AppStatus& Status(A::AgentKind a) const { return App(a); }

    // ---- tests: the same drawing code into an offscreen target, and the events without a window ----
    void SetupForTest(const std::wstring& root, Options options) {
        root_ = root; options_ = std::move(options); claude_ = options_.claude; codex_ = options_.codex; offscreen_ = true; open_ = true;
        ResetTon(); installGlyph_.SetRoot(root_);
        flow_ = AO::Flow(options_.start, options_.alone); manual_ = silent_ = false; shown_ = from_ = flow_.step(); changing_ = leaving_ = false; focus_ = hover_ = pressed_ = None; keyboard_ = false; part_ = Part::None; pointer_ = {-100, -100};
        if (flow_.step() == Step::Write) focus_ = Field;
    }
    bool OpenForTest() const { return open_; }
    void ActivateForTest(int id) { Arrange(); Activate(id, true); }
    bool KeyForTest(WPARAM key) { Arrange(); return Key(key); }
    void TypeForTest(const std::wstring& text) { Arrange(); Type(text); }
    void EraseForTest() { Erase(); }
    // A2: the pointer at (x, y) and, when `click`, a click there.
    // `measure`: a text measurer for the parts sized by their text (the title), the window having no target of its own here.
    void PointForTest(float x, float y, bool click, Chrome* measure = nullptr) {
        Chrome* keep = c_; if (measure) c_ = measure;
        Arrange(); Pointer(x, y); const Part p = PartAt(x, y); c_ = keep;
        if (!click) return; const Step before = flow_.step(); if (flow_.Pick(p)) Changed(before);
    }
    Part HoveredForTest() const { return part_; }
    int FocusForTest() const { return focus_; }
    void SetShaderSecondsForTest(double seconds) { secondsForTest_ = seconds; }
    std::vector<std::pair<int, D2D1_RECT_F>> ItemsForTest(Chrome& chrome) { Chrome* keep = c_; c_ = &chrome; Arrange(); c_ = keep; std::vector<std::pair<int, D2D1_RECT_F>> out; for (const auto& i : items_) out.push_back({i.id, i.r}); return out; }
    std::wstring BodyForTest() const { return Body(flow_.step()); }
    std::wstring TitleForTest() const { return Title(flow_.step()); }
    std::wstring PrimaryForTest() const { return PrimaryLabel(flow_.step()); }
    std::wstring SecondaryForTest() const { return SecondaryLabel(flow_.step()); }
    bool PrimaryEnabledForTest() const { return Enabled(Primary); }
    bool ManualOfferedForTest() const { return Enabled(Manual); }
    int ManualClicksForTest() const { return manualForTest_; }
    AgentStatus::Agent TargetForTest() const { return Target(); }
    float CardTopForTest() const { return CardTopAt(Motion::Now()); }
    std::wstring TroubleForTest() const { return TroubleText(); }
    bool DotsForTest() const { return !Alone(); }
    K::Rgb8 AccentForTest() const { return Accent(); }
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
