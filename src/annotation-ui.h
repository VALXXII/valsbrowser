#pragma once
// Annotation mode — native UI (V2). One include for the host (src/main.cpp via annotation-mode.h):
//   Banner + PaintToolbarButton   annotation-banner.h   (+ « Aucun agent connecté · Connecter »)
//   NotePopover                   annotation-note.h     (compact panel + « Ce que l'agent verra » pill)
//   Launcher (+ cards, history)   annotation-launcher.h, annotation-history.h
//   ConnectModal (+ veil, pill)   annotation-connect.h
//   Onboarding                    annotation-onboarding.h (V1, deprecated: replaced by ConnectModal)
// No disk I/O, no dependency on the annotation store: plain structs in, callbacks out.
// Every surface is created at its first Show (Init only stores the context). Contract,
// states and motion values: design/annotations-ui.md.
#include "annotation-banner.h"
#include "annotation-note.h"
#include "annotation-launcher.h"
#include "annotation-connect.h"
#include "annotation-onboarding.h"
