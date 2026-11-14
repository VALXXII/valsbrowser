# Profiles — implementation boundary (proposal, task_0cc0e84efa07)

Base: VALXXII/Profiles @ f665943, clean tree. No Paper mutation. `src/profile-store.h` untouched.

## Storage model
- Root = current `dataDir` (portable `<exe>\…` or installed `<root>\data`, from `ResolveBrowserPaths`/`UpdateCore::ResolveDataPaths`).
- Registry: `<root>\profiles.txt` (key=value records, written with existing `ProfileStore::CommitWith`/`WriteAtomic`, lock = `ProfileStore::Lock(root)` — reuse only, no edits).
- Profile data: default profile « Personnel » id `default` keeps the **existing** layout (`<root>\profile`, `<root>\session`, `<root>\settings`, `<root>\sessions\…`) → zero migration risk. Other profiles: `<root>\profiles\<id>\` with the same sub-layout (its own WebView2 user-data folder, settings, sessions, downloads, log).
- Window → profile: `--profile <id>` argument (absent = `default`); `WindowSessions` already scoped per dataDir (mutex hash includes dataDir), so it works unchanged per profile.

## Worker B (data/lifecycle) owns — new files only
`src/profiles.h` (header-only, no Win32 UI), `src/profile-color.h`, `tests/profiles_test.cpp`, `tests/profile_color_test.cpp`, `test-profiles.ps1`.

```cpp
namespace Profiles {
struct Profile { std::wstring id, name, icon; int hue=0; float intensity=.14f; /* 0..0.16 */ bool neutral=false; };
struct Registry { std::vector<Profile> items; };            // order = display order, default first
Registry Load(const std::wstring& root);                      // creates default « Personnel » house/neutral if missing
bool Save(const std::wstring& root, const Registry&);         // atomic, locked, merged
struct Paths { std::wstring dataDir, profileDir, sessionFile, downloadsFile, settingsFile, logFile; };
Paths ResolvePaths(const std::wstring& root, const std::wstring& id);
enum class NameError { None, Empty, TooLong, Duplicate };
NameError ValidateName(const Registry&, const std::wstring& name, const std::wstring& exceptId); // trim, 32 chars, case/accent-insensitive
std::wstring UniqueDefaultName(const Registry&);              // « Nouveau profil », « … 2 »
Profile Create(const std::wstring& root, Registry&, Profile seed); // new id, folder
// Delete = move folder to <root>\profiles\.trash\<id>-<tick>, record tombstone; Undo restores; Purge after 10 s / next start.
struct Tombstone { Profile profile; std::wstring trashDir; std::vector<std::wstring> closedSessions; };
bool Delete(const std::wstring& root, Registry&, const std::wstring& id, Tombstone& out, std::wstring& error); // refuses last profile, refuses default? (see Q)
bool Undo(const std::wstring& root, Registry&, const Tombstone&);
void PurgeTrash(const std::wstring& root);
// Live windows (for count, routing, preview)
struct WindowInfo { std::wstring sessionId; HWND hwnd; std::vector<std::wstring> titles, urls; int active; FILETIME lastActive; };
std::vector<WindowInfo> LiveWindows(const std::wstring& root, const std::wstring& id); // from WindowSessions::List + session files
}
namespace ProfileColor {   // pure math, unit-tested
struct Rgb8 { uint8_t r,g,b; };
Rgb8 Ink(int hue, float chroma);          // OKLCH L .78
Rgb8 AccentFill(int hue, float chroma);   // L solved so white text >= 4.6:1 (expect .525–.58)
Rgb8 Neutral();                           // #B3B3B3 ink / #747474 fill
bool FromHex(const std::wstring&, int& hue, float& chroma); // keeps hue, clamps L
std::wstring Hex(Rgb8);  double Contrast(Rgb8, Rgb8);
}
```
Routing helper (B): `bool Profiles::FocusOrLaunch(root, id, exe)` — brings forward the most recently active live window of that profile (`SetForegroundWindow` via HWND published in the session's live record), else spawns `exe --profile <id>`. If B needs the HWND published, B adds a sidecar `<sessions>\<id>.hwnd` written by main via a function B exposes (`Profiles::PublishWindow(paths, sessionId, hwnd)`), not by editing window-sessions.h unless agreed.

## Worker A (me, UI/integration) owns
- New `src/profile-menu.h` — popup menu (rows 32, lanes 16/flex/36/16, « Liste des onglets (n)… » first, Nouveau profil, Gérer les profils), hover tint 10 %, tab preview (400 ms open / 300 ms close, safe corridor, grouped by window, max 6 + « + n autres onglets », favicons `assets/sites/<host>.png` + initial tile), keyboard (↑↓, →/← preview, Entrée), UIA.
- New `src/profile-editor.h` — creation/edit modal content: name field, 12-icon Phosphor Fill picker, hue/intensity gradient sliders (bar thumb, keys 1/10/30, Home/End, hue wrap), hex entry, live preview plate+glyph+« Aa ».
- New `src/profile-icons.h` — 12 Phosphor Fill + menu Bold path strings (generated from @phosphor-icons/core 2.1.1, verified).
- `src/settings-panel.h` — new root row « Profils » + sub-page (list one-line rows, detail: name, icon, colour, Ouvrir une fenêtre, Supprimer) using existing page spring .34.
- `src/chrome.h` — region 900 paints the profile plate/glyph instead of the flower (private window keeps flower + chip); tooltip keycaps.
- `src/design-system.h` — `ProfileButton` tokens, radii 4/8/12/16, `Accent::Set` per window from `ProfileColor::AccentFill`, ink token; Danger unchanged.
- `src/main.cpp` — `--profile` parse, `ResolveBrowserPaths` → `Profiles::ResolvePaths`, Ctrl+Maj+M, 900 → profile menu (wheel still switches tabs), delete modal (Annuler focused, Réessayer on failure) + 10 s undo toast (« Rouvrir ses fenêtres »), broadcast of profile edits to other windows (registered message + reload registry), UIA « Profil : <nom> ».
- Tests: extend `tests/ui`, `tests/settings_test.cpp`, `tests/accessibility` for the new regions/strings.

## Sequencing
1. B lands `profile-color.h` + `profiles.h` API stubs with the exact signatures above first (compilable), then fills in + tests.
2. A builds UI against the stubs in parallel; integration once B's tests are green.
3. A runs full build (`build.ps1`) + all `test-*.ps1`; B runs its own suites.

## Accepted contract (coordinator, msg_47279ca60a9a)
- Default « Personnel » keeps the legacy root layout unchanged; other profiles get separate WebView2 user-data folders.
- Settings are per profile.
- Default profile deletable only while another active profile exists; the last profile never.
- HWND sidecar owned by B, validated by PID + window class + profile identity (no stale HWND reuse).
- No `--profile` = last-used active profile (fallback default / first active). Orphan sessions restored safely across profiles, no launch loops.
- Backend internals: profile-registry.h / profile-paths.h / profile-lifecycle.h behind the public facade `src/profiles.h`; `profile-color.h` (pure math) also owned by B.
- A (me) owns UI/main/chrome/design-system/settings/editor/icons only; contract changes go through the coordinator.
- Lifecycle: journaled, crash-recoverable multi-entry moves for the default profile; no overwrite/collision; corrupt registry fails closed (no destructive recovery), recoverable metadata preserved.
- Destructive tests in temp directories only. No release/publish.
- UI starts against the API once B posts compilable headers.
