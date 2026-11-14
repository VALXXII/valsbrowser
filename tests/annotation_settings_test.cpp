// Réglages › « Annotations et agents » (src/annotation-settings.h, src/settings-panel.h):
//  - the pure model (status lines, actions, « il y a … », summary, confirmation texts, logos = annotation-kit);
//  - the real drawer on a hidden owner: rows, Tab order, hit testing, UIA tree (names, roles, states),
//    switches (prompt re-enabled => « Plus tard » count back to 0), retention menu, folder, clear-all
//    confirmation (count named, focus on Annuler), labels that fit their boxes, PNG captures;
//  - Connecter / Déconnecter through src/agent-connect.* ONLY on temporary folders: APPDATA, LOCALAPPDATA,
//    USERPROFILE, CODEX_HOME and PATH of this process are redirected to build\annotations-v2-qa\settings\tmp
//    before anything runs (the test aborts otherwise). test-annotation-settings.ps1 fingerprints the real
//    %APPDATA%\Claude and %USERPROFILE%\.codex configurations before and after.
// Usage: annotation_settings_test.exe <repo root> <capture folder>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "../src/chrome.h"
#include "../src/settings-panel.h"
#include "../src/annotation-kit.h"
#include "../src/agent-connect.h"

using namespace NativeChrome;
namespace AS = AnnotationSettings;
namespace AC = AgentConnect;
using SettingsUI::Kind;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static void Pump(int ms = 30) { const DWORD end = GetTickCount() + ms; MSG m; while (static_cast<int>(end - GetTickCount()) > 0) while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
static std::string Utf8(const std::wstring& w) { const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr); std::string s(size_t(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr); return s; }
static std::string ReadAll(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string out; char buf[4096]; DWORD n = 0; while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n) out.append(buf, n); CloseHandle(h); return out;
}
static void WriteAll(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return; DWORD n = 0; WriteFile(h, data.data(), DWORD(data.size()), &n, nullptr); CloseHandle(h);
}
static bool StartsWithI(const std::wstring& s, const std::wstring& prefix) { return s.size() >= prefix.size() && CompareStringOrdinal(s.c_str(), int(prefix.size()), prefix.c_str(), int(prefix.size()), TRUE) == CSTR_EQUAL; }
static void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { do { const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue; const std::wstring p = dir + L"\\" + n; if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p); else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(p.c_str()); } } while (FindNextFileW(h, &fd)); FindClose(h); }
    RemoveDirectoryW(dir.c_str());
}
static const SettingsUI::Slot* Find(const std::vector<SettingsUI::Slot>& slots, Kind kind, const std::wstring& text) { for (auto& s : slots) if (s.kind == kind && s.text == text) return &s; return nullptr; }
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, long long key) { for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }
static const Accessibility::Node* NodeNamed(const std::vector<Accessibility::Node>& nodes, const std::wstring& name) { for (auto& n : nodes) if (n.name == name) return &n; return nullptr; }

// ---------------------------------------------------------------- the pure model
static void TestModel() {
    Check(AS::Ago(0) == L"à l'instant" && AS::Ago(59) == L"à l'instant" && AS::Ago(60) == L"il y a 1 min" && AS::Ago(3599) == L"il y a 59 min", "ago: seconds and minutes");
    Check(AS::Ago(3600) == L"il y a 1 h" && AS::Ago(86399) == L"il y a 23 h" && AS::Ago(86400) == L"hier" && AS::Ago(3 * 86400 + 5) == L"il y a 3 jours" && AS::Ago(-30) == L"à l'instant", "ago: hours, hier, days, clock skew");
    const long long now = 1794700000;
    AS::Agent a; Check(AS::StatusText(a, now) == L"Vérification…" && AS::ActionFor(a) == AS::Action::None, "unknown: « Vérification… », no button");
    a.known = true; Check(AS::StatusText(a, now) == L"Non installé" && AS::ActionFor(a) == AS::Action::None, "no configuration folder: « Non installé », no button");
    a.detected = true; Check(AS::StatusText(a, now) == L"Non connecté" && AS::ActionFor(a) == AS::Action::Connect && AS::ActionEnabled(a), "folder, no entry: « Non connecté » + Connecter");
    a.configured = a.connected = true; Check(AS::StatusText(a, now) == L"Connecté" && AS::ActionFor(a) == AS::Action::Disconnect, "configured: « Connecté » + Déconnecter");
    a.lastReadAt = now - 300; Check(AS::StatusText(a, now) == L"Connecté · a lu vos annotations il y a 5 min", "read: « Connecté · a lu vos annotations il y a 5 min »");
    a.stalePath = true; Check(AS::ActionFor(a) == AS::Action::Reconnect && AS::ActionLabel(AS::ActionFor(a)) == std::wstring(L"Reconnecter"), "stale path: Reconnecter");
    AS::Agent reader; reader.known = reader.detected = reader.connected = true; reader.lastReadAt = now - 90000;
    Check(AS::ActionFor(reader) == AS::Action::None && AS::StatusText(reader, now) == L"Connecté · a lu vos annotations hier", "connected by a setup we did not write: status only, nothing to undo");
    AS::Agent busy; busy.known = busy.detected = true; busy.activity = AS::Activity::Connecting;
    Check(AS::StatusText(busy, now) == L"Connexion…" && AS::ActionFor(busy) == AS::Action::Connect && !AS::ActionEnabled(busy), "connecting: « Connexion… », same button, disabled");
    busy.activity = AS::Activity::Disconnecting; Check(AS::StatusText(busy, now) == L"Déconnexion…" && AS::ActionFor(busy) == AS::Action::Disconnect, "disconnecting: « Déconnexion… »");
    AS::Agent failed; failed.known = failed.detected = true; failed.failed = true; failed.message = L"Configuration illisible : rien n'a été modifié.";
    Check(AS::StatusText(failed, now) == failed.message && AS::StatusIsError(failed) && AS::ActionFor(failed) == AS::Action::Connect, "failure: the message, in danger ink, retry possible");
    AS::Agent pending; pending.known = pending.detected = true; pending.message = L"Cliquez sur Installer dans Claude";
    Check(AS::StatusText(pending, now) == L"Cliquez sur Installer dans Claude" && !AS::StatusIsError(pending), "a step left: said plainly, not as an error");
    AS::Agents both; Check(AS::Summary(both, false).empty() && AS::Summary(both, true) == L"Aucun agent", "summary: nothing until known, then « Aucun agent »");
    both.codex.connected = true; Check(AS::Summary(both, true) == L"Codex", "summary: Codex");
    both.claude.connected = true; Check(AS::Summary(both, true) == L"Claude, Codex", "summary: Claude, Codex");
    Check(AS::ClearTitle(1) == L"Effacer l'annotation ?" && AS::ClearAction(1) == L"Effacer 1 annotation", "clear: singular");
    Check(AS::ClearTitle(12) == L"Effacer les 12 annotations ?" && AS::ClearAction(12) == L"Effacer 12 annotations" && AS::ClearBody(12).find(L"Impossible d'annuler") != std::wstring::npos, "clear: the count is named, irreversible said");
    namespace G = NativeChrome::Annotations::Glyphs;
    auto same = [](const AS::Glyph& a, const NativeChrome::Annotations::Glyph& b) { return std::strcmp(a.d, b.d) == 0 && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; };
    Check(same(AS::ClaudeLogo, G::claude) && same(AS::OpenAiLogo, G::openai) && same(AS::Mark, G::mark), "logos and mark: the exact Paper paths of annotation-kit.h");
    Check(AS::ClaudeRgb == (NativeChrome::Annotations::Ink_::claude >> 8), "Claude logo colour #D97757 (annotation-kit token)");
    // Search: the section's rows exist only when the mode is compiled in.
    Check(SettingsUI::SearchSettings(L"annotation").empty() && SettingsUI::SearchSettings(L"codex").empty(), "search without the mode: nothing about annotations");
    const auto found = SettingsUI::SearchSettings(L"annotations", true);
    Check(!found.empty() && found[0].entry->hit == SettingsUI::CatAnnotations, "search with the mode: « Annotations et agents » first");
    Check(!SettingsUI::SearchSettings(L"lanceur", true).empty() && SettingsUI::SearchSettings(L"lanceur", true)[0].entry->hit == SettingsUI::AnnotationLauncher, "search: « lanceur » finds the switch");
}

// ---------------------------------------------------------------- redirected environment (temp folders only)
struct Env {
    std::wstring base, appData, local, home, codexHome, emptyBin, app;
    AC::Environment env; AC::Target target; long long now = 1794700000;
    std::vector<BrowserAnnotations::Reader> readers;
};
static bool Redirect(Env& e, const std::wstring& root) {
    e.base = root + L"\\build\\annotations-v2-qa\\settings\\tmp";
    RemoveTree(e.base);
    CreateDirectoryW((root + L"\\build").c_str(), nullptr); CreateDirectoryW((root + L"\\build\\annotations-v2-qa").c_str(), nullptr);
    CreateDirectoryW((root + L"\\build\\annotations-v2-qa\\settings").c_str(), nullptr); CreateDirectoryW(e.base.c_str(), nullptr);
    e.appData = e.base + L"\\Roaming"; e.local = e.base + L"\\Local"; e.home = e.base + L"\\Users\\Test"; e.codexHome = e.home + L"\\.codex";
    e.emptyBin = e.base + L"\\empty-bin"; e.app = e.base + L"\\Browser";
    for (const auto* d : {&e.appData, &e.local, &e.emptyBin, &e.app}) CreateDirectoryW(d->c_str(), nullptr);
    CreateDirectoryW((e.base + L"\\Users").c_str(), nullptr); CreateDirectoryW(e.home.c_str(), nullptr);
    SetEnvironmentVariableW(L"APPDATA", e.appData.c_str()); SetEnvironmentVariableW(L"LOCALAPPDATA", e.local.c_str());
    SetEnvironmentVariableW(L"USERPROFILE", e.home.c_str()); SetEnvironmentVariableW(L"CODEX_HOME", nullptr); SetEnvironmentVariableW(L"PATH", e.emptyBin.c_str());
    e.env = AC::Environment::FromProcess();
    for (const std::wstring* v : {&e.env.appData, &e.env.localAppData, &e.env.userProfile, &e.env.path}) if (!StartsWithI(*v, e.base)) { std::printf("ABORT: environment not redirected (%s)\n", Utf8(*v).c_str()); return false; }
    e.env.now = [&e] { return static_cast<std::int64_t>(e.now); };
    e.env.mcpbAssociated = [] { return false; };                 // no .mcpb handler: the config merge path
    e.env.openFile = [](const std::wstring&) { return false; };   // never opens anything
    e.env.driveType = [](const std::wstring&) { return unsigned(DRIVE_FIXED); };
    e.target.bridgeExe = e.app + L"\\browser-annotations-mcp.exe"; e.target.storeRoot = e.app + L"\\annotations"; e.target.version = "0.3.242";
    WriteAll(e.target.bridgeExe, "MZ fake bridge");
    CreateDirectoryW(e.target.storeRoot.c_str(), nullptr);
    return true;
}
static AS::Agents Detect(const Env& e) {
    AS::Agents out; const auto d = AC::Detect(e.env);
    auto to = [&](AC::Client c, bool folder) {
        const auto st = AC::ClientStatus(e.env, c, e.target, e.readers);
        AS::Agent a; a.detected = folder; a.configured = st.configured; a.connected = st.connected(); a.stalePath = st.stalePath; a.lastReadAt = st.lastReadAt; return a;
    };
    out.claude = to(AC::Client::Claude, d.claude); out.codex = to(AC::Client::Codex, d.codex); return out;
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&cc);
    const std::wstring root = argc > 1 ? argv[1] : L".";
    const std::wstring shots = argc > 2 ? argv[2] : root + L"\\build\\annotations-v2-qa\\settings";
    Env e; if (!Redirect(e, root)) return 5;
    TestModel();

    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"AnnotationSettingsOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, 100, 100, 1100, 860, nullptr, nullptr, hi, nullptr);
    Modal::Dialog confirm; Check(confirm.Create(hi, owner, root, {}), "confirmation dialog created");

    BrowserSettings::State st;
    std::vector<BrowserSettings::Change> changes; int opened = 0, folders = 0; std::size_t count = 3, clearAsked = 0; int clears = 0;
    std::vector<ContextMenu::Item> menu; int choice = 0; bool deferConnect = false; std::function<void()> pendingConnect;
    Modal::Result confirmResult; std::wstring confirmTitle; int confirmFocus = -9; bool confirmShot = false;
    SettingsUI::Drawer::Hooks h;
    h.annotations = [] { return true; };
    h.changed = [&](BrowserSettings::Change c) { changes.push_back(c); };
    h.now = [&] { return e.now; };
    h.loadAgents = [&](std::function<void(AS::Agents)> done) { done(Detect(e)); };
    h.connectAgent = [&](AS::App app, bool on, std::function<void(bool, std::wstring)> done) {
        auto work = [&e, app, on, done] {
            AC::Report r = app == AS::App::Claude ? (on ? AC::ConnectClaude(e.env, e.target, e.base + L"\\work") : AC::DisconnectClaude(e.env, e.target))
                                                  : (on ? AC::ConnectCodex(e.env, e.target) : AC::DisconnectCodex(e.env, e.target));
            std::wstring msg; { const int n = MultiByteToWideChar(CP_UTF8, 0, r.message.data(), int(r.message.size()), nullptr, 0); msg.resize(size_t(n)); MultiByteToWideChar(CP_UTF8, 0, r.message.data(), int(r.message.size()), msg.data(), n); }
            done(r.ok(), msg);
        };
        if (deferConnect) pendingConnect = work; else work();
    };
    h.openConnect = [&] { ++opened; };
    h.annotationFolder = [&] { return e.target.storeRoot; };
    h.openAnnotationFolder = [&] { ++folders; };
    h.countAnnotations = [&](std::function<void(std::size_t)> done) { done(count); };
    h.clearAnnotations = [&](std::size_t n, bool) {
        ++clears; clearAsked = n;
        confirm.Show(SettingsUI::ClearAnnotationsSpec(n), {100, 100, 1200, 960}, [&](const Modal::Result& r) { confirmResult = r; }); confirm.FinishAnimationsForTest();
        confirmFocus = confirm.FocusForTest();
        confirmShot = confirm.RenderForTest(shots + L"\\confirm-clear-96.png", 96) && confirm.RenderForTest(shots + L"\\confirm-clear-144.png", 144);
        confirm.KeyForTest(VK_RETURN); confirm.FinishAnimationsForTest();
    };
    h.choose = [&](const std::vector<ContextMenu::Item>& items, RECT, bool) { menu = items; return choice; };
    SettingsUI::Drawer dr; Check(dr.Create(hi, owner, root, &st, h), "drawer created");
    using V = SettingsUI::View;
    const float height = 860;   // a tall window: the whole section on screen in the captures
    auto shot = [&](const wchar_t* name) {
        dr.ScrollForTest(0); dr.SettleForTest(); Pump(20);
        const bool a = dr.RenderForTest(shots + L"\\" + name + L"-96.png", 96, height, root), b = dr.RenderForTest(shots + L"\\" + name + L"-144.png", 144, height, root);
        Check(a && b, (std::string("capture ") + Utf8(name) + " (96 and 144 dpi)").c_str());
    };

    // ---- Root: the category row, its summary --------------------------------------------------
    CreateDirectoryW((e.appData + L"\\Claude").c_str(), nullptr);   // Claude Desktop « installed » (its config folder), Codex not
    dr.Show(); dr.SettleForTest();
    {
        const auto slots = dr.SlotsForTest(); const auto* cat = Find(slots, Kind::Nav, L"Annotations et agents");
        Check(cat && cat->hit == SettingsUI::CatAnnotations && cat->glyph == &AS::Mark && cat->value == L"Aucun agent", "root: « Annotations et agents » with the annotation mark, « Aucun agent »");
        const auto* display = Find(slots, Kind::Nav, L"Affichage et son"); Check(display && cat && cat->top > display->top, "root: after « Affichage et son »");
    }
    shot(L"root");
    {
        SettingsUI::Drawer plain; SettingsUI::Drawer::Hooks ph; BrowserSettings::State ps; Check(plain.Create(hi, owner, root, &ps, ph), "drawer without the mode");
        plain.Show(); plain.SettleForTest(); bool none = true; for (auto& s : plain.SlotsForTest()) none = none && s.hit != SettingsUI::CatAnnotations;
        Check(none, "mode compiled out: no « Annotations et agents » row"); plain.Hide(false, false); plain.Dispose();
    }

    // ---- The section ---------------------------------------------------------------------------
    dr.ActivateForTest(SettingsUI::CatAnnotations, false); dr.SettleForTest();
    Check(dr.CurrentViewForTest() == V::Annotations, "row opens the « Annotations et agents » view");
    if (argc > 3 && std::wstring(argv[3]) == L"--hold") {
        // Real UIA client (test-annotation-settings.ps1): the section stays on screen while it reads and
        // toggles « Afficher le lanceur d'annotations »; the state is printed at the end.
        dr.Hide(false, false); dr.Show(V::Annotations); dr.SettleForTest(); Pump(100);   // on screen (the drawer without the mode above took the activation)
        std::printf("READY\n"); std::fflush(stdout);
        const DWORD end = GetTickCount() + 12000; while (static_cast<int>(end - GetTickCount()) > 0) Pump(20);
        std::printf("launcher=%d changes=%d\n", st.annotationLauncher ? 1 : 0, static_cast<int>(changes.size())); std::fflush(stdout);
        dr.Hide(false, false); dr.Dispose(); confirm.Dispose(); DestroyWindow(owner); RemoveTree(e.base);
        return failures ? 1 : 0;
    }
    shot(L"section");
    auto slots = dr.SlotsForTest();
    {
        const auto* claude = Find(slots, Kind::Agent, L"Claude Desktop"); const auto* codex = Find(slots, Kind::Agent, L"Codex");
        Check(claude && claude->hit == SettingsUI::AgentBase && claude->value == L"Connecter" && claude->lines == std::vector<std::wstring>{L"Non connecté"} && !claude->disabled, "Claude: « Non connecté » + Connecter");
        Check(codex && codex->hit == SettingsUI::None && codex->value.empty() && codex->lines == std::vector<std::wstring>{L"Non installé"}, "Codex: « Non installé », no button");
        Check(Find(slots, Kind::Nav, L"Autre app ou réglage manuel") != nullptr, "« Autre app ou réglage manuel »");
        const auto* launcher = Find(slots, Kind::Switch, L"Afficher le lanceur d'annotations"); const auto* prompt = Find(slots, Kind::Switch, L"Proposer de connecter un agent");
        Check(launcher && prompt && !launcher->lines.empty() && !prompt->lines.empty(), "two switches with their description");
        const auto* note = Find(slots, Kind::Note, AS::ShortcutsNote); Check(note && note->hit == 0, "shortcut reminder: Ctrl+Maj+A, Ctrl+Maj+H (text, not focusable)");
        const auto* retention = Find(slots, Kind::Value, L"Conserver les terminées"); Check(retention && retention->value == L"30 jours", "retention: « 30 jours » by default");
        const auto* folder = Find(slots, Kind::Folder, L"Ouvrir le dossier des annotations"); Check(folder && folder->value == L"…\\annotations", "folder row: the store's folder name");
        const auto* clear = Find(slots, Kind::Danger, L"Effacer toutes les annotations…"); Check(clear && !clear->disabled, "« Effacer toutes les annotations… » enabled (3 annotations)");
        std::vector<std::wstring> labels; for (auto& s : slots) if (s.kind == Kind::Label) labels.push_back(s.text);
        Check(labels == std::vector<std::wstring>{L"Agents", L"Mode Annotation", L"Historique"}, "groups: Agents, Mode Annotation, Historique");
        // Every label fits its box (no clipped text): 16 DIP labels before their trailing control.
        using namespace SettingsUI::Layout; bool fit = true;
        for (auto& s : slots) {
            float right = 0, left = textLeft;
            if (s.kind == Kind::Switch) right = Switch(s.top, s.height).left - 16;
            else if (s.kind == Kind::Value) right = Chevron(s.top).left - dr.TextWidthForTest(s.value, 14) - 16;
            else if (s.kind == Kind::Nav) right = Chevron(s.top).left - 16;
            else if (s.kind == Kind::Folder) right = Trailing(s.top).left - 8;
            else if (s.kind == Kind::Danger) right = rowRight;
            else if (s.kind == Kind::Agent) { left = nameLeft; right = s.value.empty() ? rowRight : dr.AgentActionForTest(s).left - 8; }
            else continue;
            const float w = dr.TextWidthForTest(s.text, 16);
            if (w > right - left) { fit = false; std::printf("     label too wide: %s (%.1f > %.1f)\n", Utf8(s.text).c_str(), w, right - left); }
            for (auto& line : s.lines) { const float lw = dr.TextWidthForTest(line, 14); const float room = s.kind == Kind::Agent ? statusWidth : s.kind == Kind::Switch ? descriptionWidth + 8 : rowRight - textLeft; if (lw > room + .5f) { fit = false; std::printf("     line too wide: %s\n", Utf8(line).c_str()); } }
        }
        Check(fit, "every label and description line fits its box");
    }
    {
        using namespace SettingsUI;
        const std::vector<int> want{Back, Close, AgentBase, ManualConnect, AnnotationLauncher, AnnotationPrompt, AnnotationRetention, AnnotationFolder, AnnotationClear};
        Check(dr.FocusOrderForTest() == want, "Tab order: Retour, Fermer, Connecter Claude, manuel, lanceur, proposition, conservation, dossier, effacer");
        bool walked = true; for (size_t i = 0; i < want.size(); ++i) { dr.KeyForTest(VK_TAB); walked = walked && dr.FocusForTest() == want[i]; }
        dr.KeyForTest(VK_TAB); walked = walked && dr.FocusForTest() == want[0];
        Check(walked, "Tab walks that order and wraps");
        dr.KeyForTest(VK_UP); Check(dr.FocusForTest() == want.back(), "Up walks back");
        const auto* claude = Find(slots, Kind::Agent, L"Claude Desktop"); const auto button = dr.AgentActionForTest(*claude);
        Check(dr.HitForTest((button.left + button.right) / 2, (button.top + button.bottom) / 2) == AgentBase, "hit: the Connecter button");
        Check(dr.HitForTest(Layout::nameLeft + 10, claude->top + 12) == None, "hit: the row around it is inert (no accidental connect)");
        Check(button.bottom - button.top == Design::Size::compact && std::fabs((button.top + button.bottom) / 2 - (claude->top + Layout::twoLinePad + 10)) < .01f, "button: 24 DIP, centred on the name line");
    }
    {   // UIA: names, roles, states, reading order.
        using Accessibility::Role; const auto nodes = dr.AccessibleNodesForTest();
        const auto* close = Node(nodes, SettingsUI::Close); const auto* back = Node(nodes, SettingsUI::Back);
        Check(close && close->role == Role::Button && close->name == L"Fermer les réglages" && close->accelerator == L"Échap" && back && back->name == L"Retour", "UIA: Retour, Fermer les réglages (Échap)");
        Check(NodeNamed(nodes, L"Annotations et agents") && NodeNamed(nodes, L"Annotations et agents")->role == Role::Text, "UIA: the view title as text");
        const auto* connect = Node(nodes, SettingsUI::AgentBase);
        Check(connect && connect->role == Role::Button && connect->name == L"Connecter Claude Desktop" && connect->help == L"Non connecté" && connect->enabled, "UIA: « Connecter Claude Desktop », status as help");
        Check(NodeNamed(nodes, L"Codex : Non installé") && !NodeNamed(nodes, L"Codex : Non installé")->focusable, "UIA: « Codex : Non installé » read as text");
        const auto* launcher = Node(nodes, SettingsUI::AnnotationLauncher); const auto* prompt = Node(nodes, SettingsUI::AnnotationPrompt);
        Check(launcher && launcher->toggle == 2 && launcher->name == L"Afficher le lanceur d'annotations" && prompt && prompt->toggle == 2 && !prompt->help.empty(), "UIA: two toggles, on, described");
        const auto* retention = Node(nodes, SettingsUI::AnnotationRetention); Check(retention && retention->expand == 1 && retention->status == L"30 jours", "UIA: retention opens a list, value « 30 jours »");
        const auto* folder = Node(nodes, SettingsUI::AnnotationFolder); Check(folder && folder->help == e.target.storeRoot, "UIA: folder button, full path as help");
        const auto* clear = Node(nodes, SettingsUI::AnnotationClear); Check(clear && clear->enabled && clear->help == L"Effacer 3 annotations", "UIA: clear names the count");
        Check(NodeNamed(nodes, AS::ShortcutsNote) != nullptr, "UIA: the shortcut reminder is readable");
        std::vector<long long> focusables; for (auto& n : nodes) if (n.focusable && n.role != Role::Text) focusables.push_back(n.key);
        std::vector<long long> order; for (int k : dr.FocusOrderForTest()) order.push_back(k);
        Check(focusables == order, "UIA reading order of the controls = the Tab order");
    }

    // ---- Connecter Claude (temp folders), then a read by Claude ------------------------------------
    const std::wstring claudeConfig = e.appData + L"\\Claude\\claude_desktop_config.json";
    dr.ActivateForTest(SettingsUI::AgentBase, false);
    Check(ReadAll(claudeConfig).find("\"browser-annotations\"") != std::string::npos, "Connecter Claude: our entry merged into the TEMP claude_desktop_config.json");
    slots = dr.SlotsForTest();
    {
        const auto* claude = Find(slots, Kind::Agent, L"Claude Desktop");
        Check(claude && claude->value == L"Déconnecter" && claude->lines == std::vector<std::wstring>{L"Connecté"}, "Claude: « Connecté » + Déconnecter");
    }
    e.readers = {{"claude-desktop", "Claude Desktop", "", e.now - 300}}; dr.AnnotationsChanged();
    slots = dr.SlotsForTest();
    {
        const auto* claude = Find(slots, Kind::Agent, L"Claude Desktop"); std::wstring all; if (claude) for (auto& l : claude->lines) all += (all.empty() ? L"" : L" ") + l;
        Check(all == L"Connecté · a lu vos annotations il y a 5 min", "Claude read the store: « Connecté · a lu vos annotations il y a 5 min »");
    }
    dr.KeyForTest(VK_ESCAPE); dr.SettleForTest();
    { const auto* cat = Find(dr.SlotsForTest(), Kind::Nav, L"Annotations et agents"); Check(cat && cat->value == L"Claude", "root summary: « Claude »"); }
    dr.Show(V::Annotations); dr.SettleForTest();
    shot(L"claude-connected");

    // ---- Codex: folder appears, connect (TOML), busy state, disconnect -----------------------------
    CreateDirectoryW(e.codexHome.c_str(), nullptr); WriteAll(e.codexHome + L"\\config.toml", "# mine\nmodel = \"o4\"\n");
    dr.AnnotationsChanged(); slots = dr.SlotsForTest();
    { const auto* codex = Find(slots, Kind::Agent, L"Codex"); Check(codex && codex->hit == SettingsUI::AgentBase + 1 && codex->value == L"Connecter", "Codex folder present: Connecter"); }
    deferConnect = true;
    for (int guard = 0; guard < 40 && dr.FocusForTest() != SettingsUI::AgentBase + 1; ++guard) dr.KeyForTest(VK_TAB);
    dr.KeyForTest(VK_RETURN);   // Entrée on the focused « Connecter »
    slots = dr.SlotsForTest();
    {
        const auto* codex = Find(slots, Kind::Agent, L"Codex");
        Check(codex && codex->disabled && codex->lines == std::vector<std::wstring>{L"Connexion…"}, "busy: « Connexion… », button disabled");
        const auto order = dr.FocusOrderForTest(); Check(std::find(order.begin(), order.end(), SettingsUI::AgentBase + 1) != order.end() && dr.FocusForTest() == SettingsUI::AgentBase + 1, "busy: the focused button keeps the focus");
        const auto before = ReadAll(e.codexHome + L"\\config.toml"); dr.ActivateForTest(SettingsUI::AgentBase + 1, true);
        Check(pendingConnect && ReadAll(e.codexHome + L"\\config.toml") == before, "busy: a second press does nothing");
        const auto nodes = dr.AccessibleNodesForTest(); const auto* n = Node(nodes, SettingsUI::AgentBase + 1); Check(n && !n->enabled && n->help == L"Connexion…", "UIA: busy button disabled, « Connexion… »");
    }
    shot(L"codex-busy");
    deferConnect = false; if (pendingConnect) { auto run = pendingConnect; pendingConnect = nullptr; run(); }
    {
        const auto toml = ReadAll(e.codexHome + L"\\config.toml");
        Check(toml.find("[mcp_servers.browser-annotations]") != std::string::npos && toml.rfind("# mine\nmodel = \"o4\"\n", 0) == 0, "Connecter Codex: our table added to the TEMP config.toml, the rest untouched");
        slots = dr.SlotsForTest(); const auto* codex = Find(slots, Kind::Agent, L"Codex"); Check(codex && codex->value == L"Déconnecter" && !codex->disabled, "Codex: Déconnecter");
    }
    shot(L"both-connected");
    dr.ActivateForTest(SettingsUI::AgentBase + 1, false);
    Check(ReadAll(e.codexHome + L"\\config.toml").find("browser-annotations") == std::string::npos, "Déconnecter Codex: only our table removed");
    { slots = dr.SlotsForTest(); const auto* codex = Find(slots, Kind::Agent, L"Codex"); Check(codex && codex->value == L"Connecter" && codex->lines == std::vector<std::wstring>{L"Non connecté"}, "Codex: back to « Non connecté »"); }
    {   // backups: one per write, next to the file (same rules as the modal)
        int baks = 0; WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW((e.codexHome + L"\\config.toml.browser-annotations-*.bak").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) { do ++baks; while (FindNextFileW(f, &fd)); FindClose(f); }
        Check(baks >= 1, "Codex writes left timestamped backups");
    }

    // ---- Failure: a config we cannot understand ------------------------------------------------
    e.readers.clear();
    const std::string goodClaude = ReadAll(claudeConfig);
    WriteAll(claudeConfig, "{ not json");
    dr.AnnotationsChanged(); slots = dr.SlotsForTest();
    { const auto* claude = Find(slots, Kind::Agent, L"Claude Desktop"); Check(claude && claude->value == L"Connecter", "unreadable config: shown as not connected"); }
    dr.ActivateForTest(SettingsUI::AgentBase, false); slots = dr.SlotsForTest();
    {
        const auto* claude = Find(slots, Kind::Agent, L"Claude Desktop");
        Check(claude && claude->error && !claude->lines.empty() && claude->value == L"Connecter" && ReadAll(claudeConfig) == "{ not json", "failure: message in danger ink, nothing written, retry offered");
    }
    shot(L"claude-failed");
    WriteAll(claudeConfig, goodClaude);
    dr.AnnotationsChanged();
    dr.ActivateForTest(SettingsUI::AgentBase, false);
    Check(ReadAll(claudeConfig).find("browser-annotations") == std::string::npos, "Déconnecter Claude: our entry removed from the TEMP config");

    // ---- Switches ----------------------------------------------------------------------------------
    changes.clear();
    dr.ActivateForTest(SettingsUI::AnnotationLauncher, false);
    Check(!st.annotationLauncher && changes == std::vector<BrowserSettings::Change>{BrowserSettings::Change::AnnotationLauncher}, "« Afficher le lanceur d'annotations » off: state + Change::AnnotationLauncher");
    dr.ActivateForTest(SettingsUI::AnnotationLauncher, false); Check(st.annotationLauncher, "launcher back on");
    st.annotationConnectDeclines = 3; dr.Changed(); changes.clear();
    { const auto nodes = dr.AccessibleNodesForTest(); const auto* n = Node(nodes, SettingsUI::AnnotationPrompt); Check(n && n->toggle == 1, "3 « Plus tard »: the prompt switch shows off"); }
    shot(L"prompt-off");
    dr.ActivateForTest(SettingsUI::AnnotationPrompt, true);
    Check(st.annotationConnectPrompt && st.annotationConnectDeclines == 0 && changes == std::vector<BrowserSettings::Change>{BrowserSettings::Change::AnnotationConnectPrompt}, "re-enabled: « Plus tard » count back to 0");
    dr.ActivateForTest(SettingsUI::AnnotationPrompt, true);
    Check(!st.annotationConnectPrompt && st.annotationConnectDeclines == 0, "turned off: no more prompt");
    dr.ActivateForTest(SettingsUI::AnnotationPrompt, true); Check(BrowserSettings::ConnectPromptActive(st), "on again");

    // ---- Retention menu -------------------------------------------------------------------------------
    changes.clear(); choice = 3; dr.ActivateForTest(SettingsUI::AnnotationRetention, true);
    Check(menu.size() == 3 && menu[0].label == L"7 jours" && menu[1].label == L"30 jours" && menu[1].checked && menu[1].description == L"Recommandé." && menu[2].label == L"90 jours", "retention menu: 7 / 30 (checked, recommended) / 90 jours");
    Check(st.annotationRetentionDays == 90 && changes == std::vector<BrowserSettings::Change>{BrowserSettings::Change::AnnotationRetention}, "90 jours chosen: state + Change::AnnotationRetention");
    choice = 0; changes.clear(); dr.ActivateForTest(SettingsUI::AnnotationRetention, true); Check(st.annotationRetentionDays == 90 && changes.empty(), "menu dismissed: nothing changes");

    // ---- Manual setup, folder, clear all ------------------------------------------------------------
    dr.ActivateForTest(SettingsUI::ManualConnect, true); Check(opened == 1, "« Autre app ou réglage manuel » asks for the connect modal (manual fallback)");
    dr.ActivateForTest(SettingsUI::AnnotationFolder, false); Check(folders == 1, "« Ouvrir le dossier des annotations » asks the host");
    dr.ActivateForTest(SettingsUI::AnnotationClear, true);
    Check(clears == 1 && clearAsked == 3, "« Effacer toutes les annotations… »: the host confirms 3");
    Check(confirmFocus == Modal::Dialog::ButtonHit(0), "confirmation: focus starts on Annuler");
    Check(confirmResult.button == Modal::CancelButton, "confirmation: Entrée does not erase (Annuler)");
    Check(confirmShot, "confirmation captured (96 and 144 dpi)");
    {
        const auto spec = SettingsUI::ClearAnnotationsSpec(3);
        Check(spec.title == L"Effacer les 3 annotations ?" && spec.buttons.size() == 2 && spec.buttons[1].label == L"Effacer 3 annotations" && spec.buttons[1].role == Modal::Role::Danger && spec.buttons[0].isCancel, "confirmation: « Effacer 3 annotations », destructive, Échap = Annuler");
    }
    count = 0; dr.AnnotationsChanged(); slots = dr.SlotsForTest();
    {
        const auto* clear = Find(slots, Kind::Danger, L"Effacer toutes les annotations…"); const auto order = dr.FocusOrderForTest();
        Check(clear && clear->disabled && std::find(order.begin(), order.end(), SettingsUI::AnnotationClear) == order.end(), "no annotation: clear disabled and out of the Tab order");
        dr.ActivateForTest(SettingsUI::AnnotationClear, true); Check(clears == 1, "no annotation: nothing asked");
        const auto nodes = dr.AccessibleNodesForTest(); const auto* n = Node(nodes, SettingsUI::AnnotationClear); Check(n && !n->enabled && n->help == L"Aucune annotation", "UIA: « Aucune annotation »");
    }
    // Keyboard focus ring on a button, for the capture.
    count = 3; dr.AnnotationsChanged(); dr.Show(V::Annotations);
    for (int guard = 0; guard < 40 && dr.FocusForTest() != SettingsUI::AgentBase; ++guard) dr.KeyForTest(VK_TAB);
    shot(L"focus-connect");
    // Escape / Retour pops back to the root with the focus on the row that opened the section.
    dr.KeyForTest(VK_ESCAPE); dr.SettleForTest();
    Check(dr.CurrentViewForTest() == V::Main && dr.FocusForTest() == SettingsUI::CatAnnotations, "Échap: back to Réglages, focus on « Annotations et agents »");
    dr.Hide(false, false); dr.Dispose(); confirm.Dispose(); DestroyWindow(owner);
    RemoveTree(e.base);
    std::printf("\n%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
