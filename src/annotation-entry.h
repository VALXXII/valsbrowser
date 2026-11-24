#pragma once
// Annotation mode — entry points for the rest of the browser (settings section « Annotations »,
// menus). Declared here, defined in src/annotation-mode.h (no-op / empty answers when the feature is compiled
// out: --channel release and unstamped fixtures). Everything that touches the disk or another app runs off the
// UI thread; the callbacks come back on the UI thread.
//
// Settings read by the mode (src/settings.h, owned by the settings worker; until a key exists its default
// is used — detection at compile time in annotation-mode.h, no source dependency either way):
//   annotationConnectPrompt   bool  true   no longer read (« Ton agent »: « Connecter un agent » never opens by itself)
//   annotationConnectDeclines int    0     no longer read (same)
//   annotationRetentionDays   int    30    7 / 30 / 90: done annotations kept that long (Store::Purge); no settings
//                                         row since lot R (the annotation history is gone): the saved value applies
//
// Test guard: BROWSER_ANNOTATIONS_TEST=1 (or a WebView2 remote-debugging port) makes src/agent-connect refuse any
// write in the user's real Claude / Codex folders; BROWSER_ANNOTATIONS_CONNECT_ROOT redirects the connection to a
// test folder. Every automated launch of a preview copy must set both.
#include <cstddef>
#include <functional>
#include <string>

#include "annotation-settings.h"   // AnnotationSettings::ChatAgents (Réglages › Agents)

// The « Connecter un agent » window (external MCP apps), on a click only: Réglages › Agents › « Connecter un agent externe »
// (manual false) or the tutorial's « Autre app ou réglage manuel » (manual true: that part unfolded). Never by itself.
void AnnotationOpenConnect(bool manual);
// #125 v2: the mode's tutorial over the browser (fromWelcome: the welcome window's « Essayer en 1 minute », it starts at
// the trial; otherwise at its first step). Works outside the mode, on any page.
void AnnotationOpenTutorial(bool fromWelcome);
// « Ton agent » (src/agent-status.h, worker A) for Réglages › Agents: the chat agents' one status, flattened
// (src/chat-agents-view.h). Compiled out: unavailable, no « Agents » row.
bool ChatAgentsAvailable();
AnnotationSettings::ChatAgents ChatAgentsStatus();             // the status now (cheap, UI thread)
void ChatAgentsWatch(std::function<void()> changed);           // `changed` on the UI thread after every change of an agent
void ChatAgentsRefresh();                                      // detect both again (the view was opened)
// The row's button. Call it from that click (mouse or keyboard) ONLY: Install / Retry take the install's consent there
// (AgentStatus::UserClicked); SignIn opens the CLI's own sign-in. Nothing installs by itself.
void ChatAgentAct(AnnotationSettings::ChatApp app, AnnotationSettings::ChatAction action);
void AnnotationCount(std::function<void(std::size_t annotations)> done);             // published + drafts
void AnnotationClearAll(std::function<void(bool ok, std::size_t removed)> done);    // irreversible: confirm first
