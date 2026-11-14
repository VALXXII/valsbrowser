#pragma once
// Annotation mode — native UI (V1). One include for the host (src/main.cpp, T3):
//   Banner + PaintToolbarButton   annotation-banner.h
//   NotePopover                   annotation-note.h
//   Launcher (+ card)             annotation-launcher.h
//   Onboarding (+ veil)           annotation-onboarding.h
// No disk I/O, no dependency on the annotation store: plain structs in, callbacks out.
// Every surface is created at its first Show (Init only stores the context). Contract,
// states and motion values: design/annotations-ui.md.
#include "annotation-banner.h"
#include "annotation-note.h"
#include "annotation-launcher.h"
#include "annotation-onboarding.h"
