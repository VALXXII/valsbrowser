// Réglages › « Annotations » (src/annotation-settings.h, src/settings-panel.h):
//  - the pure model (confirmation texts, logos = annotation-kit);
//  - the real drawer on an owner far off screen: rows, Tab order, UIA tree (names, roles, states), retention menu,
//    folder, clear-all confirmation (count named, focus on Annuler), labels that fit their boxes, PNG captures;
//  - « Ton agent » (worker C): the section no longer lists agents — no MCP rows (Claude Desktop / Codex), no « Autre
//    app ou réglage manuel », no « Proposer de connecter un agent » (Réglages › Agents: tests/agents_settings_test.cpp);
//  - APPDATA, LOCALAPPDATA, USERPROFILE, CODEX_HOME and PATH of this process are still redirected to
//    build\annotations-v2-qa\settings\tmp before anything runs (the test aborts otherwise), and
//    test-annotation-settings.ps1 fingerprints the real %APPDATA%\Claude and %USERPROFILE%\.codex configurations.
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
static bool StartsWithI(const std::wstring& s, const std::wstring& prefix) { return s.size() >= prefix.size() && CompareStringOrdinal(s.c_str(), int(prefix.size()), prefix.c_str(), int(prefix.size()), TRUE) == CSTR_EQUAL; }
static void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { do { const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue; const std::wstring p = dir + L"\\" + n; if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p); else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(p.c_str()); } } while (FindNextFileW(h, &fd)); FindClose(h); }
    RemoveDirectoryW(dir.c_str());
}
static const SettingsUI::Slot* Find(const std::vector<SettingsUI::Slot>& slots, Kind kind, const std::wstring& text) { for (auto& s : slots) if (s.kind == kind && s.text == text) return &s; return nullptr; }
// A control's node by its hit (the drawer keys controls SettingsUI::Drawer::UiaKey(hit)).
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, int hit) { const long long key = SettingsUI::Drawer::UiaKey(hit); for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }
static const Accessibility::Node* NodeNamed(const std::vector<Accessibility::Node>& nodes, const std::wstring& name) { for (auto& n : nodes) if (n.name == name) return &n; return nullptr; }

// ---------------------------------------------------------------- the pure model
static void TestModel() {
    Check(AS::ClearTitle(1) == L"Effacer l'annotation ?" && AS::ClearAction(1) == L"Effacer 1 annotation", "clear: singular");
    Check(AS::ClearTitle(12) == L"Effacer les 12 annotations ?" && AS::ClearAction(12) == L"Effacer 12 annotations" && AS::ClearBody(12).find(L"Impossible d'annuler") != std::wstring::npos, "clear: the count is named, irreversible said");
    namespace G = NativeChrome::Annotations::Glyphs;
    auto same = [](const AS::Glyph& a, const NativeChrome::Annotations::Glyph& b) { return std::strcmp(a.d, b.d) == 0 && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; };
    Check(same(AS::ClaudeLogo, G::claude) && same(AS::OpenAiLogo, G::openai) && same(AS::Mark, G::mark), "logos and mark: the exact Paper paths of annotation-kit.h");
    Check(AS::ClaudeRgb == (NativeChrome::Annotations::Ink_::claude >> 8), "Claude logo colour #D97757 (annotation-kit token)");
    // Search: the section's rows exist only when the mode is compiled in.
    Check(SettingsUI::SearchSettings(L"annotation").empty() && SettingsUI::SearchSettings(L"codex").empty(), "search without the mode: nothing about annotations");
    const auto found = SettingsUI::SearchSettings(L"annotations", true);
    Check(!found.empty() && found[0].entry->hit == SettingsUI::CatAnnotations, "search with the mode: « Annotations » first");
    {   // The section is titled « Annotations » (user's copy, Paper p-K-0); the agents stay findable by their words.
        Check(std::wstring(found[0].entry->title) == L"Annotations" && std::wstring(found[0].entry->path) == L"Réglages", "search: « Annotations » · Réglages");
        bool cat = false; for (const auto& r : SettingsUI::SearchSettings(L"agents", true)) cat = cat || r.entry->hit == SettingsUI::CatAnnotations;
        Check(cat, "search: « agents » still finds the section");
        bool mcp = false; for (const auto& r : SettingsUI::SearchSettings(L"claude", true)) mcp = mcp || std::wstring(r.entry->path) == L"Annotations › Agents";
        Check(!mcp, "search: no agent row under « Annotations » any more (« Ton agent »: Réglages › Agents)");
        for (const auto& r : SettingsUI::SearchSettings(L"annot", true)) Check(std::wstring(r.entry->path).find(L"et agents") == std::wstring::npos && std::wstring(r.entry->title).find(L"et agents") == std::wstring::npos, "search: no « Annotations et agents » left");
    }
    {   // V3: no launcher switch any more (the launcher only exists in the mode).
        bool none = true; for (const auto& r : SettingsUI::SearchSettings(L"lanceur", true)) if (std::wstring(r.entry->title).find(L"lanceur") != std::wstring::npos) none = false;
        Check(none, "search: no « Afficher le lanceur d'annotations » row (V3)");
        bool prompt = false; for (const auto& r : SettingsUI::SearchSettings(L"proposer", true)) prompt = prompt || std::wstring(r.entry->title) == L"Proposer de connecter un agent";
        Check(!prompt, "search: no « Proposer de connecter un agent » (« Ton agent »: the window never opens by itself)");
    }
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
    // The relay must be a real program for this machine (agent-connect BridgeUsable, since 1d658b5: PE header and machine
    // checked): a copy of this test executable (a « MZ » text file is refused, nothing gets written).
    { wchar_t self[MAX_PATH]{}; GetModuleFileNameW(nullptr, self, MAX_PATH); if (!CopyFileW(self, e.target.bridgeExe.c_str(), FALSE)) { std::printf("ABORT: no relay copy\n"); return false; } }
    CreateDirectoryW(e.target.storeRoot.c_str(), nullptr);
    return true;
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
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, -32000, -32000, 1100, 860, nullptr, nullptr, hi, nullptr);   // off screen
    Modal::Dialog confirm; Check(confirm.Create(hi, owner, root, {}), "confirmation dialog created");

    BrowserSettings::State st;
    std::vector<BrowserSettings::Change> changes; int folders = 0; std::size_t count = 3, clearAsked = 0; int clears = 0;
    std::vector<ContextMenu::Item> menu; int choice = 0;
    Modal::Result confirmResult; std::wstring confirmTitle; int confirmFocus = -9; bool confirmShot = false;
    SettingsUI::Drawer::Hooks h;
    h.annotations = [] { return true; };
    h.changed = [&](BrowserSettings::Change c) { changes.push_back(c); };
    h.now = [&] { return e.now; };
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
        const auto slots = dr.SlotsForTest(); const auto* cat = Find(slots, Kind::Nav, L"Annotations");
        Check(cat && cat->hit == SettingsUI::CatAnnotations && cat->glyph == &AS::Mark && cat->value.empty(), "root: « Annotations » with the annotation mark, no agent summary (« Ton agent »: the « Agents » row)");
        const auto* display = Find(slots, Kind::Nav, L"Affichage et son"); Check(display && cat && cat->top > display->top, "root: after « Affichage et son »");
    }
    shot(L"root");
    {
        SettingsUI::Drawer plain; SettingsUI::Drawer::Hooks ph; BrowserSettings::State ps; Check(plain.Create(hi, owner, root, &ps, ph), "drawer without the mode");
        plain.Show(); plain.SettleForTest(); bool none = true; for (auto& s : plain.SlotsForTest()) none = none && s.hit != SettingsUI::CatAnnotations;
        Check(none, "mode compiled out: no « Annotations » row"); plain.Hide(false, false); plain.Dispose();
    }
    {   // With Profils (main #69): both sections side by side, distinct hits, both reachable (Tab, UIA, navigation).
        SettingsUI::Drawer both; SettingsUI::Drawer::Hooks bh = h; BrowserSettings::State bs;
        bh.profiles = [] { SettingsUI::ProfileItem a; a.id = L"default"; a.name = L"Personnel"; a.current = true; SettingsUI::ProfileItem b; b.id = L"p2"; b.name = L"Travail"; return std::vector<SettingsUI::ProfileItem>{a, b}; };
        Check(both.Create(hi, owner, root, &bs, bh), "drawer with Profils and Annotations");
        both.ShowProfiles(); both.SettleForTest(); both.KeyForTest(VK_ESCAPE); both.SettleForTest();   // profiles read, back on the root
        const auto rs = both.SlotsForTest(); const auto* prof = Find(rs, Kind::Nav, L"Profils"); const auto* ann = Find(rs, Kind::Nav, L"Annotations");
        Check(prof && ann && prof->hit == SettingsUI::CatProfiles && ann->hit == SettingsUI::CatAnnotations && prof->hit != ann->hit && prof->value == L"Personnel", "root: « Profils » (Personnel) and « Annotations », distinct hits");
        const auto order = both.FocusOrderForTest();
        Check(std::find(order.begin(), order.end(), SettingsUI::CatProfiles) != order.end() && std::find(order.begin(), order.end(), SettingsUI::CatAnnotations) != order.end(), "root Tab order reaches both rows");
        const auto nodes = both.AccessibleNodesForTest(); const auto* pn = Node(nodes, SettingsUI::CatProfiles); const auto* an = Node(nodes, SettingsUI::CatAnnotations);
        Check(pn && pn->automationId == L"SettingsProfiles" && pn->name == L"Profils, Personnel" && an && an->name == L"Annotations" && an->status.empty(), "UIA root: « Profils, Personnel » (SettingsProfiles) and « Annotations »");
        both.ActivateForTest(SettingsUI::CatProfiles, true); both.SettleForTest();
        const auto pnodes = both.AccessibleNodesForTest(); int items = 0; for (auto& n : pnodes) items += n.automationId == L"ProfileListItem";
        Check(both.CurrentViewForTest() == SettingsUI::View::Profiles && items == 2, "« Profils » still opens its list (2 ProfileListItem)");
        both.KeyForTest(VK_ESCAPE); both.SettleForTest(); both.ActivateForTest(SettingsUI::CatAnnotations, true); both.SettleForTest();
        Check(both.CurrentViewForTest() == SettingsUI::View::Annotations && Node(both.AccessibleNodesForTest(), SettingsUI::AnnotationClear), "« Annotations » still opens its section");
        both.Hide(false, false); both.Dispose();
    }

    // ---- The section ---------------------------------------------------------------------------
    dr.ActivateForTest(SettingsUI::CatAnnotations, false); dr.SettleForTest();
    Check(dr.CurrentViewForTest() == V::Annotations, "row opens the « Annotations » view");
    if (argc > 3 && std::wstring(argv[3]) == L"--hold") {
        // Real UIA client (test-annotation-settings.ps1): the section stays open while it reads it; the state is
        // printed at the end (nothing may change by reading).
        dr.Hide(false, false); dr.Show(V::Annotations); dr.SettleForTest(); Pump(100);   // on screen (the drawer without the mode above took the activation)
        std::printf("READY\n"); std::fflush(stdout);
        const DWORD end = GetTickCount() + 12000; while (static_cast<int>(end - GetTickCount()) > 0) Pump(20);
        std::printf("prompt=%d changes=%d\n", st.annotationConnectPrompt ? 1 : 0, static_cast<int>(changes.size())); std::fflush(stdout);
        dr.Hide(false, false); dr.Dispose(); confirm.Dispose(); DestroyWindow(owner); RemoveTree(e.base);
        return failures ? 1 : 0;
    }
    shot(L"section");
    auto slots = dr.SlotsForTest();
    {
        bool agentRows = false; for (auto& s : slots) agentRows = agentRows || s.kind == Kind::Agent || s.kind == Kind::Switch;
        Check(!agentRows && !Find(slots, Kind::Nav, L"Autre app ou réglage manuel"), "« Ton agent »: no agent rows, no switch, no « Autre app ou réglage manuel » (Réglages › Agents has them)");
        const auto* note = Find(slots, Kind::Note, AS::ShortcutsNote()); Check(note && note->hit == 0, "shortcut reminder: Ctrl+Maj+A, Ctrl+Maj+H (text, not focusable)");
        const auto* retention = Find(slots, Kind::Value, L"Conserver les terminées"); Check(retention && retention->value == L"30 jours", "retention: « 30 jours » by default");
        const auto* folder = Find(slots, Kind::Folder, L"Ouvrir le dossier des annotations"); Check(folder && folder->value == L"…\\annotations", "folder row: the store's folder name");
        const auto* clear = Find(slots, Kind::Danger, L"Effacer toutes les annotations…"); Check(clear && !clear->disabled, "« Effacer toutes les annotations… » enabled (3 annotations)");
        std::vector<std::wstring> labels; for (auto& s : slots) if (s.kind == Kind::Label) labels.push_back(s.text);
        Check(labels == std::vector<std::wstring>{L"Mode Annotation", L"Historique"}, "groups: Mode Annotation, Historique (« Ton agent »: no Agents group)");
        // Every label fits its box (no clipped text): 16 DIP labels before their trailing control.
        using namespace SettingsUI::Layout; bool fit = true;
        for (auto& s : slots) {
            float right = 0, left = textLeft;
            if (s.kind == Kind::Switch) right = Switch(s.top, s.height).left - 16;
            else if (s.kind == Kind::Value) right = Chevron(s.top).left - dr.TextWidthForTest(s.value, 14) - 16;
            else if (s.kind == Kind::Nav) right = Chevron(s.top).left - 16;
            else if (s.kind == Kind::Folder) right = Trailing(s.top).left - 8;
            else if (s.kind == Kind::Danger) right = rowRight;
            else continue;
            const float w = dr.TextWidthForTest(s.text, 16);
            if (w > right - left) { fit = false; std::printf("     label too wide: %s (%.1f > %.1f)\n", Utf8(s.text).c_str(), w, right - left); }
            for (auto& line : s.lines) { const float lw = dr.TextWidthForTest(line, 14); const float room = s.kind == Kind::Switch ? descriptionWidth + 8 : rowRight - textLeft; if (lw > room + .5f) { fit = false; std::printf("     line too wide: %s\n", Utf8(line).c_str()); } }
        }
        Check(fit, "every label and description line fits its box");
    }
    {
        using namespace SettingsUI;
        const std::vector<int> want{Back, Close, AnnotationRetention, AnnotationFolder, AnnotationClear};
        Check(dr.FocusOrderForTest() == want, "Tab order: Retour, Fermer, conservation, dossier, effacer");
        bool walked = true; for (size_t i = 0; i < want.size(); ++i) { dr.KeyForTest(VK_TAB); walked = walked && dr.FocusForTest() == want[i]; }
        dr.KeyForTest(VK_TAB); walked = walked && dr.FocusForTest() == want[0];
        Check(walked, "Tab walks that order and wraps");
        dr.KeyForTest(VK_UP); Check(dr.FocusForTest() == want.back(), "Up walks back");
    }
    {   // UIA: names, roles, states, reading order.
        using Accessibility::Role; const auto nodes = dr.AccessibleNodesForTest();
        const auto* close = Node(nodes, SettingsUI::Close); const auto* back = Node(nodes, SettingsUI::Back);
        Check(close && close->role == Role::Button && close->name == L"Fermer les réglages" && close->accelerator == L"Échap" && back && back->name == L"Retour", "UIA: Retour, Fermer les réglages (Échap)");
        Check(NodeNamed(nodes, L"Annotations") && NodeNamed(nodes, L"Annotations")->role == Role::Group && NodeNamed(nodes, L"Annotations")->automationId == L"SettingsDrawer", "UIA: the drawer pane is named after the view");
        Check(!Node(nodes, SettingsUI::AgentBase) && !NodeNamed(nodes, L"Proposer de connecter un agent") && !NodeNamed(nodes, L"Afficher le lanceur d'annotations"), "UIA: no agent button, no toggle");
        const auto* retention = Node(nodes, SettingsUI::AnnotationRetention); Check(retention && retention->expand == 1 && retention->status == L"30 jours", "UIA: retention opens a list, value « 30 jours »");
        const auto* folder = Node(nodes, SettingsUI::AnnotationFolder); Check(folder && folder->help == e.target.storeRoot, "UIA: folder button, full path as help");
        const auto* clear = Node(nodes, SettingsUI::AnnotationClear); Check(clear && clear->enabled && clear->help == L"Effacer 3 annotations", "UIA: clear names the count");
        Check(NodeNamed(nodes, AS::ShortcutsNote()) != nullptr, "UIA: the shortcut reminder is readable");
        std::vector<long long> focusables; for (auto& n : nodes) if (n.focusable && n.role != Role::Text && n.role != Role::Group) focusables.push_back(n.key);
        std::vector<long long> order; for (int k : dr.FocusOrderForTest()) order.push_back(SettingsUI::Drawer::UiaKey(k));
        Check(focusables == order, "UIA reading order of the controls = the Tab order");
    }

    // ---- Retention menu -------------------------------------------------------------------------------
    changes.clear(); choice = 3; dr.ActivateForTest(SettingsUI::AnnotationRetention, true);
    Check(menu.size() == 3 && menu[0].label == L"7 jours" && menu[1].label == L"30 jours" && menu[1].checked && menu[1].description == L"Recommandé." && menu[2].label == L"90 jours", "retention menu: 7 / 30 (checked, recommended) / 90 jours");
    Check(st.annotationRetentionDays == 90 && changes == std::vector<BrowserSettings::Change>{BrowserSettings::Change::AnnotationRetention}, "90 jours chosen: state + Change::AnnotationRetention");
    choice = 0; changes.clear(); dr.ActivateForTest(SettingsUI::AnnotationRetention, true); Check(st.annotationRetentionDays == 90 && changes.empty(), "menu dismissed: nothing changes");

    // ---- Folder, clear all ---------------------------------------------------------------------------
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
    for (int guard = 0; guard < 40 && dr.FocusForTest() != SettingsUI::AnnotationRetention; ++guard) dr.KeyForTest(VK_TAB);
    shot(L"focus-retention");
    // Escape / Retour pops back to the root with the focus on the row that opened the section.
    dr.KeyForTest(VK_ESCAPE); dr.SettleForTest();
    Check(dr.CurrentViewForTest() == V::Main && dr.FocusForTest() == SettingsUI::CatAnnotations, "Échap: back to Réglages, focus on « Annotations »");
    dr.Hide(false, false); dr.Dispose(); confirm.Dispose(); DestroyWindow(owner);
    RemoveTree(e.base);
    std::printf("\n%d checks, %d failures\n", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
