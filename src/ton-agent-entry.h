#pragma once
// « Ton agent » — the ONE entry point to the agents' screen (Paper « Onboarding agents — Ton agent (maquette prête) »):
// the chat (its « Choisissez votre agent » block) and the annotation band's reminder open this same screen.
//
// Declared here by worker C (the band's reminder, src/annotation-mode.h); DEFINED by worker B (the tutorial's « Ton agent »
// step, src/annotation-onboarding*.h). B's definition comes with `#define BROWSER_TON_AGENT_SCREEN 1` placed before
// src/annotation-mode.h's end: until then annotation-mode.h defines a stand-in (it logs and opens the chat panel, whose
// own block shows the agents). Called on the UI thread, inside the mode or outside it.
enum class TonAgentFrom { Band, Chat, Settings };
void OpenTonAgent(TonAgentFrom from);
