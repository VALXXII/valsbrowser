#pragma once
// « Ton agent » — the ONE entry point to the agents' screen (Paper « Onboarding agents — Ton agent (maquette prête) »):
// the chat (its « Choisissez votre agent » block) and the annotation band's reminder open this same screen.
//
// Declared here by worker C (the band's reminder, src/annotation-mode.h); DEFINED by worker B (the tutorial's « Ton agent »
// step, src/annotation-onboarding*.h). B's definition comes with `#define BROWSER_TON_AGENT_SCREEN 1` placed before
// src/annotation-mode.h's end: until then annotation-mode.h defines a stand-in (it logs and opens the chat panel, whose
// own block shows the agents). Called on the UI thread, inside the mode or outside it.
#include <string>
enum class TonAgentFrom { Band, Chat, Settings };
void OpenTonAgent(TonAgentFrom from);
// « Connexion dans un autre onglet » (Paper note 265A-0), defined with the screen (src/chat-ton-agent.h):
//   TonAgentTabChanged  after every change of the tab in front (AnnotationTabChanged): the screen alone steps aside while
//                       a sign-in's page is in front and comes back over the tab it was opened over;
//   TonAgentPageOpened  a link handed over by another launch opened in a tab (main.cpp OpenHandedTarget): a CLI's
//                       sign-in page when Browser is the default browser — kept for « Rouvrir la page de connexion ↗ ».
void TonAgentTabChanged();
void TonAgentPageOpened(const std::wstring& url);
