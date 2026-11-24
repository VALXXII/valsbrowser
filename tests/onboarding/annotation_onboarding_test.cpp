// Annotation mode tutorial (#125 v2): the flow (pure), the first-time rule and its setting, then the seven steps, the
// live-site card (E1b) drawn off screen by the production windows (software Direct2D into WIC, the shader through
// Direct3D) at 96 and 120 DPI, saved as PNG for the side-by-side audit with the Paper boards
// (design/annotation-onboarding-evidence/), in the boards' cyan, in violet and in « Personnel »'s blue; plus the checks that
// need no eyes: Paper geometry, the guided trial (hover, pick, type, send, the button grows), every button and key, A6
// « Ton agent » and A7 on the agents' one status (src/agent-status.h, detections injected: never a real CLI), both
// languages. Never opens a window, never touches a profile. A6's own states: tests/onboarding/ton_agent_test.cpp.
// Usage: annotation_onboarding_test.exe <repo root> <output dir>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "../../src/settings.h"
#include "../../src/annotation-onboarding.h"
#include "../../src/annotation-local-card.h"

// The agents' one status with injected detections (no CLI, no installer, no console): Claude Code ready, Codex signed out
// (the boards' « Claude connected, Codex not »), or nothing installed.
static AgentRuntime::Detection Det(AgentRuntime::Agent a, bool installed, bool signedIn) {
    AgentRuntime::Detection d; d.agent = a; d.cli.state = installed ? AgentRuntime::CliState::Found : AgentRuntime::CliState::Missing;
    d.login = signedIn ? AgentRuntime::Login::SignedIn : AgentRuntime::Login::SignedOut; if (installed) { d.cli.version = "2.1.325"; d.cli.path = L"C:\\fake\\cli.exe"; }
    return d;
}
static void Agents(bool claudeReady) {
    AgentStatus::Host h; h.dataRoot = L"C:\\nonexistent-browser-test-data";
    h.environment = [] { AgentRuntime::Environment e; e.dataRoot = L"C:\\nonexistent-browser-test-data"; e.freshUserEnvironment = false; return e; };
    h.detect = [claudeReady](const AgentRuntime::Environment&, AgentRuntime::Agent a) { return a == AgentRuntime::Agent::Claude ? Det(a, claudeReady, true) : Det(a, claudeReady, false); };
    AgentStatus::Init(h);   // synchronous (no `run`): the statuses are there at once
}

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static bool Near(float a, float b, float eps = .51f) { return std::fabs(a - b) <= eps; }

namespace UI = NativeChrome::AnnotationOnboardingUI; namespace AO = AnnotationOnboarding; namespace A = NativeChrome::Annotations; namespace K = NativeChrome::OnboardingKit;
using AO::Step; using AO::Part;

static void FlowTests() {
    AO::Flow f;
    Expect(f.step() == Step::What && f.open() && f.ending() == AO::Ending::None && AO::StepCount == 7, "flow: seven steps, opens on A1 « Bienvenue »");
    Expect(!f.Back() && f.step() == Step::What, "flow: A1's secondary is « Plus tard » (not a step back)");
    Expect(f.Next() && f.step() == Step::Pick, "flow: A1 « Suivant » → A2 « choisissez un élément »");
    Expect(!f.Pick(Part::Title) && !f.Pick(Part::Email) && !f.Pick(Part::Header) && f.step() == Step::Pick, "trial: a click on anything but « Payer » does not pick");
    f.Suggest(L"Rends ce bouton plus large");   // what the window gives (annotation_onboarding_write_hint)
    Expect(f.text().empty(), "trial: the note is written only once « Payer » is picked");
    Expect(f.Pick(Part::Pay) && f.step() == Step::Write && f.text() == L"Rends ce bouton plus large", "trial: « Payer » picked → A3 « Envoyez la demande », the note already written");
    Expect(f.CanSend(), "trial: A3's « Envoyer » is on at once (nothing to type)");
    f.SetText(L"   "); Expect(!f.CanSend() && !f.Send() && f.step() == Step::Write, "trial: emptied (blanks only), nothing to send");
    f.SetText(L"Rends ce bouton plus large"); Expect(f.Send() && f.step() == Step::Result && f.tried(), "trial: the note sent → A4, the button wider");
    Expect(f.Back() && f.step() == Step::Pick && !f.tried() && f.text().empty(), "trial: A4 « Recommencer » → A2, everything as at the start");
    Expect(f.Pick(Part::Pay) && f.text() == L"Rends ce bouton plus large" && f.CanSend(), "trial: « Recommencer » then « Payer »: the note written again, « Envoyer » on");
    f.SetText(L"Autre chose"); Expect(f.Send() && f.step() == Step::Result, "trial: any text makes it wider (the demo does not read it)");
    AO::Flow g; g.Suggest(L"Rends ce bouton plus large"); g.Next(); g.Pick(Part::Pay);
    g.Suggest(L"Make this button wider"); Expect(g.text() == L"Make this button wider" && g.CanSend(), "trial: a new language rewrites the untouched note");
    g.SetText(L"Plus large"); g.Suggest(L"Rends ce bouton plus large"); Expect(g.text() == L"Plus large", "trial: an edited note is kept when the language changes");
    Expect(f.Next() && f.step() == Step::Where, "flow: A4 « Suivant » → A5 « Où ça marche »");
    Expect(f.Back() && f.step() == Step::Result && f.Next(), "flow: A5 « Retour » → A4 once the trial was done");
    Expect(f.Next() && f.step() == Step::Connect && f.Later() && f.step() == Step::Ready, "flow: A5 → A6 « Ton agent » → (« Plus tard ») A7 « Prêt »");
    Expect(f.Back() && f.step() == Step::Connect && f.Later(), "flow: A7 « Retour » → A6");
    Expect(f.Next() && f.step() == Step::Closed && f.ending() == AO::Ending::Finished && !f.open(), "flow: A7 « Terminer » ends it (finished)");
    Expect(!f.Next() && !f.Back() && !f.Skip() && f.ending() == AO::Ending::Finished, "flow: nothing moves once closed");
    AO::Flow s; s.Next(); Expect(s.Next() && s.step() == Step::Where, "flow: A2 « Passer l'essai » → A5");
    Expect(s.Back() && s.step() == Step::Pick, "flow: A5 « Retour » → A2 when the trial was skipped");
    AO::Flow w; w.Next(); w.Pick(Part::Pay); Expect(w.Next() && w.step() == Step::Where, "flow: A3 « Passer l'essai » → A5");
    AO::Flow e(Step::Pick); Expect(e.step() == Step::Pick && AO::Index(e.step()) == 1, "flow: the welcome window's « Essayer en 1 minute » starts at A2");
    Expect(e.Skip() && e.ending() == AO::Ending::Skipped, "flow: Échap / close ends it (skipped)");
    Expect(AO::Trial(Step::Pick) && AO::Trial(Step::Write) && AO::Trial(Step::Result) && !AO::Trial(Step::Where), "trial: A2 – A4");
    Expect(std::string(AO::TagOf(Part::Pay)) == "button" && std::string(AO::TagOf(Part::Title)) == "h2" && std::string(AO::TagOf(Part::Email)) == "input" && std::string(AO::TagOf(Part::Header)) == "header", "trial: the tags the frame shows");
    // The first-time rule.
    AO::Launch l;
    Expect(AO::ShouldShow(l), "rule: a new person sees it the first time the mode is on");
    l.onboarded = true; Expect(!AO::ShouldShow(l), "rule: never again by itself once finished or skipped");
    l.forced = true; Expect(AO::ShouldShow(l), "rule: « --annotation-onboarding » shows it every time (QA)");
    AO::Launch d; d.direct = true; Expect(!AO::ShouldShow(d), "rule: not when the mode starts on an element from the page menu");
    AO::Launch c; c.chatOpen = true; Expect(!AO::ShouldShow(c), "rule: not while the chat was asked for");
    AO::Launch b; b.busy = true; b.forced = true; Expect(!AO::ShouldShow(b), "rule: not over another modal or the note panel");
    Expect(Onboarding::HasSwitch(L"--annotation-onboarding", AO::Switch) && !Onboarding::HasSwitch(L"--annotation-onboarding-x", AO::Switch), "rule: the switch is matched as a whole argument");
    Expect(AO::ReaderOf(true, false) == AO::Reader::Claude && AO::ReaderOf(false, true) == AO::Reader::Codex && AO::ReaderOf(false, false) == AO::Reader::None, "A7: the READY agent (Claude Code first), none otherwise");
    // The setting that remembers it.
    BrowserSettings::State st; Expect(!st.annotationOnboarded, "setting: a new profile has not seen it");
    st.annotationOnboarded = true; std::stringstream out; BrowserSettings::Write(out, st);
    std::stringstream in(out.str()); const auto back = BrowserSettings::Read(in);
    Expect(out.str().find("annotation-onboarded=1\n") != std::string::npos && back.annotationOnboarded && back == st, "setting: written and read back (finished or skipped stays so)");
}

static Profiles::Profile Violet() { Profiles::Profile p; p.hue = 290; p.intensity = ProfileColor::MaxChroma; p.neutral = false; return p; }
static Profiles::Profile Blue() { Profiles::Profile p; int hue = 267; float chroma = 0; ProfileColor::FromHex(L"#3E67FF", hue, chroma); p.hue = hue; p.intensity = chroma; p.neutral = false; return p; }

struct Fixture {
    UI::Window window; std::wstring root, out;
    int connects = 0, closed = 0; A::AgentKind asked = A::AgentKind::Other; AO::Ending ending = AO::Ending::None; bool manual = false;
    void Setup(A::AppStatus claude = {}, A::AppStatus codex = {}, Step start = Step::What, const Profiles::Profile* tint = nullptr, unsigned accent = 0) {
        UI::Options o; o.reduced = true; o.claude = claude; o.codex = codex; o.start = start; if (tint) o.tint = *tint; o.accent = accent;
        o.hooks.connect = [this](A::AgentKind a) { ++connects; asked = a; };
        o.hooks.closed = [this](AO::Ending e, bool m) { ++closed; ending = e; manual = m; };
        window.SetupForTest(root, o); window.SetShaderSecondsForTest(0);
    }
    bool Shot(const std::wstring& name) {
        bool ok = true;
        for (UINT dpi : {96u, 120u}) { const std::wstring path = out + L"\\" + name + (dpi == 96 ? L"@96.png" : L"@120.png"); ok = window.RenderForTest(dpi, path.c_str()) && ok; }
        return ok;
    }
    D2D1_RECT_F Item(int id, NativeChrome::Chrome& c) { for (const auto& i : window.ItemsForTest(c)) if (i.first == id) return i.second; return {}; }
};

// A2 – A4 in one colour, as the boards draw them: the pointer on « Payer » (A2), the note written (A3), the result (A4).
static void Trial(Fixture& f, const std::wstring& suffix, const Profiles::Profile* tint, unsigned accent) {
    const A::AppStatus connected{A::ConnectState::Connected, L""};
    f.Setup(connected, {}, Step::Pick, tint, accent);
    f.window.PointForTest(308, 248, false);   // Paper A2: the crosshair centred at (308, 248), over the button
    Expect(f.window.HoveredForTest() == Part::Pay, "A2: the pointer over « Payer » frames it");
    Expect(f.Shot(L"A2" + suffix), "render: A2 « Essayez : choisissez un élément »");
    f.window.PointForTest(308, 248, true);
    Expect(f.window.Flow().step() == Step::Write, "A2: the click on « Payer » picks it → A3");
    Expect(f.window.Flow().text() == L"Rends ce bouton plus large" && f.window.Flow().CanSend() && !f.window.NoteActiveForTest(), "A3: the note arrives written, « Envoyer » on and ringed, no caret (1S2M-0, 1S33-0)");
    Expect(f.Shot(L"A3" + suffix), "render: A3 « Envoyez la demande » (the note written, « Envoyer » highlighted)");
    f.window.ActivateForTest(UI::Window::Send);
    Expect(f.window.Flow().step() == Step::Result, "A3: « Envoyer » → A4");
    Expect(f.Shot(L"A4" + suffix), "render: A4 « Et voilà : le bouton est plus large »");
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 3) { std::printf("usage: annotation_onboarding_test <repo root> <output dir>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    I18n::SetForProcess(I18n::Locale::Fr);
    FlowTests();

    NativeChrome::Chrome measure; HWND scratch = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, -32000, -32000, 10, 10, nullptr, nullptr, nullptr, nullptr);
    const bool measured = scratch && SUCCEEDED(measure.Init(scratch, argv[1]));
    Expect(measured, "setup: a text measurer");

    const A::AppStatus connected{A::ConnectState::Connected, L""}, idle{};   // (the MCP status A6 no longer shows)
    Fixture f; f.root = argv[1]; f.out = argv[2];
    Agents(true);
    // ---- the seven steps as Paper draws them (cyan; A6 / A7: Claude Code ready, Codex to sign in) ----
    f.Setup(connected, idle);
    Expect(f.Shot(L"A1"), "render: A1 « Bienvenue sur le mode Annotation »");
    Expect(f.window.AccentForTest().r == 0x00 && f.window.AccentForTest().g == 0x99 && f.window.AccentForTest().b == 0xA0, "colour: the boards' cyan by default (#0099A0)");
    if (measured) {
        const auto later = f.Item(UI::Window::Secondary, measure), next = f.Item(UI::Window::Primary, measure);
        Expect(Near(later.left, 19) && Near(later.top, 531) && Near(later.bottom - later.top, 32), "A1 geometry: « Plus tard » at (19, 531), 32 high");
        Expect(Near(next.right, 701) && Near(next.top, 531), "A1 geometry: « Suivant » right-aligned at 701");
        const auto lang = f.Item(UI::Window::Lang, measure), close = f.Item(UI::Window::Close, measure);
        Expect(Near(lang.left, 12) && Near(lang.top, 10) && Near(close.left, 680) && Near(close.right - close.left, 28), "A1 geometry: language at (12, 10), close 28 at (680, 10)");
    }
    f.window.ActivateForTest(UI::Window::Secondary);
    Expect(f.closed == 1 && f.ending == AO::Ending::Skipped && !f.manual && !f.window.OpenForTest(), "A1 « Plus tard »: closed, skipped (never shown again by itself)");

    // The guided trial, step by step.
    f.Setup(connected, idle); f.closed = 0;
    f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.window.Flow().step() == Step::Pick, "A1 « Suivant » → A2");
    if (measured) {
        const auto pay = f.Item(UI::Window::Payer, measure), skip = f.Item(UI::Window::Primary, measure);
        Expect(Near(pay.left, 166) && Near(pay.top, 228) && Near(pay.right - pay.left, 160) && Near(pay.bottom - pay.top, 36), "A2 geometry: « Payer » 160 × 36 at (166, 228) (1S1C-0)");
        Expect(Near(skip.right, 701), "A2 geometry: « Passer l'essai » right-aligned at 701 (1RWN-0)");
    }
    f.window.PointForTest(600, 400, false); Expect(f.window.HoveredForTest() == Part::None, "A2: off the page, nothing framed");
    f.window.PointForTest(200, 165, false); Expect(f.window.HoveredForTest() == Part::Email, "A2: the e-mail field framed under the pointer");
    f.window.PointForTest(200, 165, true); Expect(f.window.Flow().step() == Step::Pick, "A2: a click on the field does not pick");
    f.window.PointForTest(170, 125, false, measured ? &measure : nullptr); Expect(f.window.HoveredForTest() == Part::Title, "A2: the title framed");
    f.window.PointForTest(400, 80, false); Expect(f.window.HoveredForTest() == Part::Header, "A2: the site's header framed");
    f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == UI::Window::Payer, "A2 keyboard: Tab reaches « Payer »");
    f.window.KeyForTest(VK_RETURN); Expect(f.window.Flow().step() == Step::Write && f.window.FocusForTest() == UI::Window::Field, "A2 keyboard: Entrée on « Payer » picks it; the note takes the keys");
    Expect(f.window.Flow().text() == L"Rends ce bouton plus large" && f.window.Flow().CanSend() && !f.window.NoteActiveForTest(), "A3: the note arrives written, « Envoyer » on, no caret");
    f.window.EraseForTest(); f.window.TypeForTest(L"x"); f.window.EraseForTest(); f.window.TypeForTest(L"e");
    Expect(f.window.Flow().text() == L"Rends ce bouton plus large" && f.window.NoteActiveForTest(), "A3: the note stays editable (erase, type); the caret shows once edited");
    f.window.KeyForTest(VK_RETURN);
    Expect(f.window.Flow().step() == Step::Result && f.window.Flow().tried(), "A3: Entrée sends at once (« Envoyer » is on)");
    f.window.ActivateForTest(UI::Window::Secondary);
    Expect(f.window.Flow().step() == Step::Pick && f.window.Flow().text().empty(), "A4 « Recommencer » → A2");
    f.window.PointForTest(308, 248, true);
    Expect(f.window.Flow().step() == Step::Write && f.window.Flow().text() == L"Rends ce bouton plus large" && f.window.Flow().CanSend() && !f.window.NoteActiveForTest(), "A4 « Recommencer » then « Payer »: the note written again, « Envoyer » on, no caret");
    if (measured) { const auto send = f.Item(UI::Window::Send, measure); Expect(Near(send.left, 569) && Near(send.top, 240), "A3 geometry: « Envoyer » 28 × 28 at (569, 240) (1S33-0)"); }
    f.window.ActivateForTest(UI::Window::Field); Expect(f.window.NoteActiveForTest(), "A3: a click in the note shows the caret");
    f.window.ActivateForTest(UI::Window::Send);
    Expect(f.window.Flow().step() == Step::Result && f.window.Flow().tried(), "A3 « Envoyer » → A4");
    f.window.ActivateForTest(UI::Window::Secondary);
    Expect(f.window.Flow().step() == Step::Pick && f.window.Flow().text().empty(), "A4 « Recommencer » → A2, the note empty until « Payer »");
    f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.window.Flow().step() == Step::Where, "A2 « Passer l'essai » → A5");
    Expect(f.Shot(L"A5"), "render: A5 « Sur vos projets en cours »");
    f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.window.Flow().step() == Step::Connect, "A5 « Suivant » → A6");
    Expect(f.Shot(L"A6"), "render: A6 « Ton agent » (Claude Code ready, Codex to sign in: Paper « Claude Code · 3 Prêt »)");
    if (measured) {
        const auto claude = f.Item(UI::Window::ClaudeRow, measure), codex = f.Item(UI::Window::CodexRow, measure);
        Expect(Near(claude.left, 172) && Near(claude.top, 152) && Near(claude.right, 355) && Near(claude.bottom, 288) && Near(codex.left, 365) && Near(codex.right, 548), "A6 geometry: the tiles 183 × 136 at (172, 152) and x 365 (card at 140)");
    }
    f.window.SetStatus(A::AgentKind::Codex, {A::ConnectState::Connected, L""});   // the MCP status: no longer shown
    Expect(f.connects == 0 && f.window.SecondaryForTest() == L"Plus tard" && f.window.PrimaryForTest() == L"Continuer" && f.window.PrimaryEnabledForTest(), "A6: Claude Code ready → « Plus tard », « Continuer » (no MCP row)");
    Expect(f.window.TileNameForTest(AgentStatus::Agent::Codex) == L"Se connecter à Codex" && f.window.TileEnabledForTest(AgentStatus::Agent::Codex) && !f.window.TileEnabledForTest(AgentStatus::Agent::Claude),
           "A6: the Codex tile's click signs in (« Se connecter à Codex »); the ready Claude Code tile has no click");
    f.window.ActivateForTest(UI::Window::Secondary);
    Expect(f.window.Flow().step() == Step::Ready, "A6 « Plus tard » → A7");
    Expect(f.Shot(L"A7"), "render: A7 « Tout est prêt »");
    Expect(f.window.BodyForTest().find(L"Claude Code s’occupe du reste.") != std::wstring::npos, "A7: the sentence names the READY agent");
    f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.closed == 1 && f.ending == AO::Ending::Finished && !f.window.OpenForTest(), "A7 « Terminer »: closed, finished");

    // ---- other states ----
    Agents(false);
    f.Setup(connected, connected, Step::Ready); f.closed = 0;   // the MCP says « connected »: A7 still says what the chat would say
    Expect(f.Shot(L"A7-no-agent") && f.window.BodyForTest().find(L"Votre agent s’occupe du reste.") != std::wstring::npos, "A7 with no agent ready (even « connected » by MCP): grey dot, « Aucun agent prêt », « Votre agent »");
    f.window.KeyForTest(VK_ESCAPE); Expect(f.closed == 1 && f.ending == AO::Ending::Skipped, "Échap: closed, skipped");
    f.Setup(idle, idle, Step::Connect); f.closed = 0; f.window.ActivateForTest(UI::Window::Manual);
    Expect(f.closed == 0 && !f.manual && f.window.OpenForTest(), "A6: no « Autre app ou réglage manuel » any more (Réglages › Agents)");
    Agents(true);
    f.Setup(idle, idle, Step::Write); f.closed = 0;
    for (int i = 0; i < 40 && !f.window.Flow().text().empty(); ++i) f.window.EraseForTest();
    Expect(f.window.Flow().text().empty() && !f.window.Flow().CanSend(), "A3 emptied by the person: nothing to send");
    Expect(f.Shot(L"A3-empty"), "render: A3 emptied (the hint, « Envoyer » dimmed, no ring)");

    // ---- the trial in the boards' cyan, in violet and in « Personnel »'s blue ----
    Trial(f, L"", nullptr, 0);
    const auto violet = Violet(); Trial(f, L"-violet", &violet, 0);
    Expect(!(f.window.AccentForTest().r == 0x00 && f.window.AccentForTest().g == 0x99), "colour: a violet profile moves the accent off the boards' cyan");
    const auto blue = Blue(); Trial(f, L"-personnel", &blue, 0x3E67FF);
    Expect(f.window.AccentForTest().r == 0x3E && f.window.AccentForTest().g == 0x67 && f.window.AccentForTest().b == 0xFF, "colour: « Personnel » takes the mode's blue #3E67FF");
    f.Setup(connected, idle, Step::What, &violet, 0); Expect(f.Shot(L"A1-violet"), "render: A1 in violet");

    // ---- E1b: the live-site card ----
    {
        NativeChrome::AnnotationLocalCard::Card card; int discover = 0, closedCard = 0;
        NativeChrome::AnnotationLocalCard::Hooks h; h.discover = [&] { ++discover; }; h.closed = [&] { ++closedCard; };
        card.SetupForTest(argv[1], L"boutique-sable.fr", 0x0099A0, h);
        if (measured) Expect(Near(card.HeightForTest(measure), 178), "E1b geometry: the card 316 × 178 (1STF-0)");
        bool ok = true;
        for (UINT dpi : {96u, 120u}) { const std::wstring path = f.out + L"\\E1b-card" + (dpi == 96 ? L"@96.png" : L"@120.png"); ok = card.RenderForTest(dpi, path.c_str(), measure) && ok; }
        Expect(ok, "render: E1b « Annoter marche sur vos projets et fichiers locaux »");
        card.ActivateForTest(NativeChrome::AnnotationLocalCard::Card::Discover);
        Expect(discover == 1 && closedCard == 1, "E1b « Découvrir en 1 minute »: the tutorial, the card gone");
    }
    // ---- E1b over the toolbar's tooltips (Fix, PR #131: E1a's tip stayed on top of the card's title) ----
    {
        namespace T = NativeChrome::Tooltip;
        T::Tip tip; const bool made = scratch && tip.Create(GetModuleHandleW(nullptr), scratch, argv[1]);   // never shown: no message loop runs its timer
        const T::Content e1a{TR(annotate_local_only), L"", L""}, star{L"Ajouter aux favoris", L"Ctrl+D", L""};
        const RECT annoter{1053, 48, 1077, 72}, favoris{1000, 48, 1024, 72};
        if (made) {
            tip.Hover(e1a, annoter); Expect(tip.Pending(), "tooltip: the greyed button's tip on its way (600 ms of rest)");
            tip.Block(true);   // the card opens (ShowLocalCard)
            Expect(tip.Blocked() && !tip.Pending() && !tip.Visible(), "E1b: the card opening dismisses the button's tip at once");
            tip.Hover(e1a, annoter); Expect(!tip.Pending() && !tip.Visible(), "E1b: the pointer « moving » over the button again (the card's appearance) brings no tip back");
            tip.Hover(star, favoris); Expect(!tip.Pending(), "E1b: no other toolbar tip while the card is open");
            tip.Show(star, favoris); Expect(!tip.Visible(), "E1b: not even an immediate one (keyboard focus)");
            tip.Block(false);  // the card gone (its `closed` hook)
            tip.Hover(star, favoris); Expect(!tip.Blocked() && tip.Pending(), "E1b: the tips come back once the card is gone");
            tip.HideNow();
        } else Expect(false, "setup: a tooltip window (hidden)");
        tip.Dispose();
    }
    // ---- E1a for screen readers: the greyed button's HelpText is the tooltip's sentence ----
    {
        NativeChrome::Chrome bar;
        bar.SetEnabled('N', true); bar.annotateTip = TR(chrome_tip_annotate);
        Expect(bar.AnnotateHelp() == TR(a11y_annotate_help), "UIA: on a local page the help says how to annotate");
        bar.annotateOff = true; bar.annotateTip = TR(annotate_local_only);   // a live site: greyed but clickable (E1b)
        Expect(bar.AnnotateHelp() == L"Mode Annotation - seulement sur vos projets et fichiers locaux", "UIA: on a live site the greyed button's help is E1a's text, word for word");
        bar.annotateOff = false; bar.SetEnabled('N', false); bar.annotateTip = L"Annoter — indisponible";
        Expect(bar.AnnotateHelp() == L"Annoter — indisponible", "UIA: any other unavailable state keeps its tooltip as help");
    }

    // ---- English ----
    I18n::SetForProcess(I18n::Locale::En);
    f.Setup(connected, idle);
    Expect(f.Shot(L"en-A1"), "render: A1 in English");
    f.window.ActivateForTest(UI::Window::Primary); Expect(f.Shot(L"en-A2"), "render: A2 in English");
    f.window.ActivateForTest(UI::Window::Primary); Expect(f.Shot(L"en-A5"), "render: A5 in English");
    f.window.ActivateForTest(UI::Window::Primary); Expect(f.Shot(L"en-A6"), "render: A6 in English");
    f.Setup(connected, idle, Step::Ready); Expect(f.Shot(L"en-A7"), "render: A7 in English");
    Expect(f.window.BodyForTest().find(L"Ctrl + Shift + A") != std::wstring::npos, "A7 in English: the shortcut's own names");
    f.Setup(connected, idle, Step::Pick); f.window.PointForTest(308, 248, true);
    Expect(f.window.Flow().text() == L"Make this button wider" && f.window.Flow().CanSend(), "A3 in English: « Make this button wider » already written, « Send » on");
    Expect(f.Shot(L"en-A3"), "render: A3 in English « Send the request »");
    I18n::SetForProcess(I18n::Locale::Fr);

    AgentStatus::Shutdown();
    if (scratch) { measure.Dispose(); DestroyWindow(scratch); }
    std::printf("%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
