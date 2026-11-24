// Annotation mode onboarding (#125, variant A): the flow (pure), the first-time rule and its setting, then the four
// boards drawn off screen by the production window (software Direct2D into WIC, the shader through Direct3D) at 96
// and 120 DPI, saved as PNG for the side-by-side audit with the Paper boards (design/annotation-onboarding-evidence/),
// plus the checks that need no eyes: Paper geometry of the interactive parts, the events of each button and key,
// A3's connected state, both languages. Never opens a window, never touches a profile.
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

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static bool Near(float a, float b, float eps = .51f) { return std::fabs(a - b) <= eps; }

namespace UI = NativeChrome::AnnotationOnboardingUI; namespace AO = AnnotationOnboarding; namespace A = NativeChrome::Annotations;
using AO::Step;

static void FlowTests() {
    AO::Flow f;
    Expect(f.step() == Step::What && f.open() && f.ending() == AO::Ending::None, "flow: opens on A1 « À quoi ça sert »");
    Expect(!f.Back() && f.step() == Step::What, "flow: no « Retour » on A1");
    Expect(f.Next() && f.step() == Step::Where, "flow: A1 « Suivant » → A2");
    Expect(f.Next() && f.step() == Step::Connect, "flow: A2 « Suivant » → A3");
    Expect(f.Back() && f.step() == Step::Where && f.Next() && f.step() == Step::Connect, "flow: « Retour » goes one step back, « Suivant » forward again");
    Expect(f.Next() && f.step() == Step::Ready, "flow: A3 « Continuer » → A4");
    Expect(f.Back() && f.step() == Step::Connect && f.Next(), "flow: A4 « Retour » → A3");
    Expect(f.Next() && f.step() == Step::Closed && f.ending() == AO::Ending::Finished && !f.open(), "flow: A4 « Annoter ma page » ends it (finished)");
    Expect(!f.Next() && !f.Back() && !f.Skip() && f.ending() == AO::Ending::Finished, "flow: nothing moves once closed");
    AO::Flow g; g.Next(); g.Next();
    Expect(g.Skip() && g.step() == Step::Closed && g.ending() == AO::Ending::Skipped, "flow: Échap / close on A3 ends it (skipped)");
    Expect(AO::Index(Step::What) == 0 && AO::Index(Step::Ready) == 3 && AO::Index(Step::Closed) == -1 && AO::StepCount == 4, "flow: four progress steps");
    // The first-time rule.
    AO::Launch l;
    Expect(AO::ShouldShow(l), "rule: a new person sees it the first time the mode is on");
    l.onboarded = true; Expect(!AO::ShouldShow(l), "rule: never again once finished or skipped");
    l.forced = true; Expect(AO::ShouldShow(l), "rule: « --annotation-onboarding » shows it every time (QA)");
    AO::Launch d; d.direct = true; Expect(!AO::ShouldShow(d), "rule: not when the mode starts on an element from the page menu (the note first)");
    AO::Launch c; c.chatOpen = true; Expect(!AO::ShouldShow(c), "rule: not while the chat was asked for");
    AO::Launch b; b.busy = true; b.forced = true; Expect(!AO::ShouldShow(b), "rule: not over another modal or the note panel");
    Expect(Onboarding::HasSwitch(L"--annotation-onboarding", AO::Switch) && !Onboarding::HasSwitch(L"--annotation-onboarding-x", AO::Switch) && !Onboarding::HasSwitch(L"--onboarding", AO::Switch), "rule: the switch is matched as a whole argument");
    // Who reads the notes (A4).
    Expect(AO::ReaderOf(AO::Link::Connected, AO::Link::Idle) == AO::Reader::Claude && AO::ReaderOf(AO::Link::Idle, AO::Link::Connected) == AO::Reader::Codex
           && AO::ReaderOf(AO::Link::Connected, AO::Link::Connected) == AO::Reader::Claude && AO::ReaderOf(AO::Link::Working, AO::Link::Failed) == AO::Reader::None, "A4: the connected agent (Claude first), none otherwise");
    Expect(AO::Connectable(AO::Link::Idle) && AO::Connectable(AO::Link::Failed) && AO::Connectable(AO::Link::Reconnect) && !AO::Connectable(AO::Link::Working) && !AO::Connectable(AO::Link::Connected), "A3: a row connects only from a resting state");
    // The setting that remembers it.
    BrowserSettings::State s; Expect(!s.annotationOnboarded, "setting: a new profile has not seen it");
    s.annotationOnboarded = true; std::stringstream out; BrowserSettings::Write(out, s);
    Expect(out.str().find("annotation-onboarded=1\n") != std::string::npos, "setting: written as annotation-onboarded=1");
    std::stringstream in(out.str()); const auto back = BrowserSettings::Read(in);
    Expect(back.annotationOnboarded && back == s, "setting: read back (finished or skipped stays so)");
    std::stringstream old("browser-settings 1\nengine=duckduckgo\n"); Expect(!BrowserSettings::Read(old).annotationOnboarded, "setting: an older settings file (no key) has not seen it");
}

struct Fixture {
    UI::Window window; std::wstring root, out;
    int connects = 0, closed = 0; A::AgentKind asked = A::AgentKind::Other; AO::Ending ending = AO::Ending::None; bool manual = false;
    void Setup(A::AppStatus claude = {}, A::AppStatus codex = {}) {
        UI::Options o; o.reduced = true; o.claude = claude; o.codex = codex;
        o.hooks.connect = [this](A::AgentKind a) { ++connects; asked = a; };
        o.hooks.closed = [this](AO::Ending e, bool m) { ++closed; ending = e; manual = m; };
        window.SetupForTest(root, o); window.SetShaderSecondsForTest(0);
    }
    bool Shot(const wchar_t* name) {
        bool ok = true;
        for (UINT dpi : {96u, 120u}) { const std::wstring path = out + L"\\" + name + (dpi == 96 ? L"@96.png" : L"@120.png"); ok = window.RenderForTest(dpi, path.c_str()) && ok; }
        return ok;
    }
    D2D1_RECT_F Item(int id, NativeChrome::Chrome& c) { for (const auto& i : window.ItemsForTest(c)) if (i.first == id) return i.second; return {}; }
};

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 3) { std::printf("usage: annotation_onboarding_test <repo root> <output dir>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    I18n::SetForProcess(I18n::Locale::Fr);
    FlowTests();

    // A text measurer at 96 DPI for the geometry (the window's own Chrome is the render target's).
    NativeChrome::Chrome measure; HWND scratch = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, -32000, -32000, 10, 10, nullptr, nullptr, nullptr, nullptr);
    const bool measured = scratch && SUCCEEDED(measure.Init(scratch, argv[1]));
    Expect(measured, "setup: a text measurer");

    const A::AppStatus connected{A::ConnectState::Connected, L""}, idle{};
    Fixture f; f.root = argv[1]; f.out = argv[2];
    // ---- A1 … A4 as Paper draws them (A3 / A4: Claude connected, Codex not) ----
    f.Setup(connected, idle);
    Expect(f.Shot(L"A1"), "render: A1 « À quoi ça sert »");
    if (measured) {
        const auto later = f.Item(UI::Window::Secondary, measure), next = f.Item(UI::Window::Primary, measure);
        Expect(Near(later.left, 19) && Near(later.top, 531) && Near(later.bottom - later.top, 32), "A1 geometry: « Plus tard » at (19, 531), 32 high (1MN5-0)");
        Expect(Near(next.right, 701) && Near(next.top, 531), "A1 geometry: « Suivant » right-aligned at 701 (1MN1-0)");
        const auto lang = f.Item(UI::Window::Lang, measure), close = f.Item(UI::Window::Close, measure);
        Expect(Near(lang.left, 12) && Near(lang.top, 10) && Near(lang.bottom - lang.top, 28) && Near(close.left, 680) && Near(close.right - close.left, 28), "A1 geometry: language at (12, 10), close 28 at (680, 10) (1MNB-0)");
    }
    // Échap on A1 is « Plus tard ».
    f.window.ActivateForTest(UI::Window::Secondary);
    Expect(f.closed == 1 && f.ending == AO::Ending::Skipped && !f.manual && !f.window.OpenForTest(), "A1 « Plus tard »: closed, skipped (never shown again by the host)");

    f.Setup(connected, idle); f.closed = 0;
    f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.window.Flow().step() == Step::Where, "A1 « Suivant » → A2");
    Expect(f.Shot(L"A2"), "render: A2 « Où ça marche »");
    if (measured) Expect(Near(f.Item(UI::Window::Secondary, measure).left, 19), "A2 geometry: « Retour » at x 19 (1MOU-0)");
    f.window.ActivateForTest(UI::Window::Secondary);
    Expect(f.window.Flow().step() == Step::What && f.closed == 0, "A2 « Retour » → A1 (still open)");
    f.window.ActivateForTest(UI::Window::Primary); f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.window.Flow().step() == Step::Connect, "A2 « Suivant » → A3");
    Expect(f.Shot(L"A3"), "render: A3 « Connecter » (Claude connected, Codex to connect)");
    if (measured) {
        const auto claude = f.Item(UI::Window::ClaudeRow, measure), codex = f.Item(UI::Window::CodexRow, measure);
        Expect(Near(claude.left, 192) && Near(claude.top, 124) && Near(claude.right, 528) && Near(claude.bottom, 164), "A3 geometry: Claude's row 336 × 40 at (192, 124) (1MTV-0)");
        Expect(Near(codex.top, 172) && Near(codex.bottom, 212), "A3 geometry: Codex's row at y 172 (1MU1-0)");
        const auto cont = f.Item(UI::Window::Primary, measure); Expect(Near(cont.right, 701), "A3 geometry: « Continuer » at the right (1MQF-0)");
    }
    // A3's connected state: the row is a status, not a button; Codex's row connects.
    f.window.ActivateForTest(UI::Window::ClaudeRow);
    Expect(f.connects == 0, "A3: « Claude est connecté » does nothing when clicked");
    f.window.ActivateForTest(UI::Window::CodexRow);
    Expect(f.connects == 1 && f.asked == A::AgentKind::Codex, "A3: « Connecter avec Codex » asks the host to connect Codex");
    f.window.SetStatus(A::AgentKind::Codex, {A::ConnectState::Working, L""});
    Expect(f.Shot(L"A3-codex-working"), "render: A3 while Codex connects");
    f.window.ActivateForTest(UI::Window::CodexRow);
    Expect(f.connects == 1, "A3: a row being connected is not clicked again");
    f.window.SetStatus(A::AgentKind::Codex, {A::ConnectState::Failed, L"config.toml illisible"});
    Expect(f.Shot(L"A3-codex-failed"), "render: A3 after a failure (the modal's message)");
    f.window.SetStatus(A::AgentKind::Codex, {A::ConnectState::Connected, TRF(agent_restart_to_enable, L"Codex")});
    Expect(f.window.Status(A::AgentKind::Codex).state == A::ConnectState::Connected, "A3: the host's answer is shown");
    Expect(f.Shot(L"A3-both-connected"), "render: A3 with both agents connected (restart note)");
    // Keyboard: Tab goes through what can be used, Entrée is the main button.
    f.window.SetStatus(A::AgentKind::Codex, idle);
    f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == UI::Window::CodexRow, "A3 keyboard: Tab skips the connected row and lands on Codex's");
    f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == UI::Window::Manual, "A3 keyboard: then « Autre app ou réglage manuel »");
    f.window.KeyForTest(VK_TAB); f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == UI::Window::Primary, "A3 keyboard: then « Retour », « Continuer »");
    f.window.KeyForTest(VK_RETURN);
    Expect(f.window.Flow().step() == Step::Ready && f.window.FocusForTest() == UI::Window::Primary, "A3 « Continuer » (Entrée) → A4, the keyboard on « Annoter ma page »");
    Expect(f.Shot(L"A4-keyboard"), "render: A4 reached from the keyboard (focus ring on the main button)");
    Expect(f.window.BodyForTest().find(L"Claude s’occupe du reste.") != std::wstring::npos, "A4: the sentence names the connected agent");
    f.window.KeyForTest(VK_RETURN);
    Expect(f.closed == 1 && f.ending == AO::Ending::Finished && !f.window.OpenForTest(), "A4 Entrée: closed, finished");
    // A4 as Paper draws it (reached with the mouse: no focus ring).
    f.Setup(connected, idle); f.closed = 0;
    for (int i = 0; i < 3; ++i) f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.window.Flow().step() == Step::Ready && f.Shot(L"A4"), "render: A4 « Prêt »");
    if (measured) { const auto go = f.Item(UI::Window::Primary, measure); Expect(Near(go.right, 701) && Near(go.top, 531), "A4 geometry: « Annoter ma page » right-aligned at 701 (1MS4-0)"); }
    f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.closed == 1 && f.ending == AO::Ending::Finished && !f.window.OpenForTest(), "A4 « Annoter ma page »: closed, finished");

    // ---- other states ----
    f.Setup(idle, idle); f.closed = 0; f.window.ActivateForTest(UI::Window::Primary); f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.Shot(L"A3-none-connected"), "render: A3 with no agent connected yet");
    f.window.ActivateForTest(UI::Window::Primary);
    Expect(f.window.Flow().step() == Step::Ready, "A3 « Continuer » works without an agent");
    Expect(f.Shot(L"A4-no-agent"), "render: A4 with no agent connected");
    Expect(f.window.BodyForTest().find(L"Votre agent s’occupe du reste.") != std::wstring::npos, "A4: with no agent, the sentence says « Votre agent »");
    f.window.KeyForTest(VK_ESCAPE);
    Expect(f.closed == 1 && f.ending == AO::Ending::Skipped, "Échap on A4: closed, skipped");
    f.Setup(idle, idle); f.closed = 0; f.window.ActivateForTest(UI::Window::Primary); f.window.ActivateForTest(UI::Window::Primary);
    f.window.ActivateForTest(UI::Window::Manual);
    Expect(f.closed == 1 && f.manual && f.ending == AO::Ending::Skipped, "A3 « Autre app ou réglage manuel »: closed, the host opens the manual setup");
    f.Setup(idle, idle); f.closed = 0;
    f.window.ActivateForTest(UI::Window::Close);
    Expect(f.closed == 1 && f.ending == AO::Ending::Skipped, "the close button: closed, skipped");

    // ---- English ----
    I18n::SetForProcess(I18n::Locale::En);
    f.Setup(connected, idle);
    Expect(f.Shot(L"en-A1"), "render: A1 in English");
    f.window.ActivateForTest(UI::Window::Primary); Expect(f.Shot(L"en-A2"), "render: A2 in English");
    f.window.ActivateForTest(UI::Window::Primary); Expect(f.Shot(L"en-A3"), "render: A3 in English");
    f.window.ActivateForTest(UI::Window::Primary); Expect(f.Shot(L"en-A4"), "render: A4 in English");
    Expect(f.window.BodyForTest().find(L"Ctrl + Shift + A") != std::wstring::npos, "A4 in English: the shortcut's own names");
    I18n::SetForProcess(I18n::Locale::Fr);

    if (scratch) { measure.Dispose(); DestroyWindow(scratch); }
    std::printf("%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
