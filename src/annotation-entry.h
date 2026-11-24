#pragma once
// Annotation mode — entry points for the rest of the browser (settings section « Annotations »,
// menus). Declared here, defined in src/annotation-mode.h (no-op / empty answers when the feature is compiled
// out: --channel release and unstamped fixtures). Everything that touches the disk or another app runs off the
// UI thread; the callbacks come back on the UI thread.
//
// Settings read by the mode (src/settings.h, owned by the settings worker; until a key exists its default
// is used — detection at compile time in annotation-mode.h, no source dependency either way):
//   (annotation-launcher: V2 key, read and ignored since V3 — the launcher only exists in the mode)
//   annotationConnectPrompt   bool  true   « Proposer de connecter un agent au lancement du mode »
//   annotationConnectDeclines int    0     « Plus tard » answers (the modal stops opening by itself at 3)
//   annotationRetentionDays   int    30    7 / 30 / 90: done annotations kept that long (Store::Purge)
// After changing one of them, call AnnotationSettingsChanged() (launcher shown/hidden at once).
//
// Test guard: BROWSER_ANNOTATIONS_TEST=1 (or a WebView2 remote-debugging port) makes src/agent-connect refuse any
// write in the user's real Claude / Codex folders; BROWSER_ANNOTATIONS_CONNECT_ROOT redirects the connection to a
// test folder. Every automated launch of a preview copy must set both.
#include <cstddef>
#include <functional>
#include <string>

struct AnnotationAgentState {
    bool detected = false;      // the app's configuration folder exists (%APPDATA%\Claude, CODEX_HOME / %USERPROFILE%\.codex)
    bool configured = false;    // our entry is in its configuration (Claude: or the .mcpb extension is installed)
    bool connected = false;     // configured, or it read the store recently (readers\, 7 days)
    bool stalePath = false;     // configured, but for another bridge/store path: « Reconnecter »
    bool removable = false;     // the browser/store is on a removable drive
    long long lastReadAt = 0;   // UTC seconds, 0 = never
    std::wstring lastRead;      // « il y a 3 min » (empty = never)
    std::wstring readerLabel;   // « Claude Desktop », « Codex »… as the bridge saw it
    bool restart = false;       // configured, but the app has not read the store since: « Redémarrez Claude pour l'activer »
};
enum class AnnotationApp { Claude = 0, Codex = 1 };

void AnnotationSettingsChanged();
// The « Connecter un agent » modal (not the launch variant); manual = « Autre app ou réglage manuel » unfolded.
void AnnotationOpenConnect(bool manual);
// #125 v2: the mode's tutorial over the browser (fromWelcome: the welcome window's « Essayer en 1 minute », it starts at
// the trial; otherwise at its first step). Works outside the mode, on any page.
void AnnotationOpenTutorial(bool fromWelcome);
// Same code paths as the modal (src/agent-connect.h: backup + atomic write, .mcpb when associated, codex CLI
// when present; BROWSER_ANNOTATIONS_CONNECT_ROOT redirects every write). message: French, for the UI.
void AnnotationConnectAgent(AnnotationApp app, bool connect, std::function<void(bool ok, const std::wstring& message)> done);
void AnnotationAgentsStatus(std::function<void(const AnnotationAgentState& claude, const AnnotationAgentState& codex)> done);
std::wstring AnnotationStoreRoot();                                                  // the store folder (L"" when compiled out)
void AnnotationOpenFolder();                                                        // Explorer on the store root
void AnnotationCount(std::function<void(std::size_t annotations)> done);             // published + drafts
void AnnotationClearAll(std::function<void(bool ok, std::size_t removed)> done);    // irreversible: confirm first
