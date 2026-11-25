#pragma once
// Annotation mode — native UI (V2). One include for the host (src/main.cpp via annotation-mode.h):
//   Banner + PaintToolbarButton   annotation-banner.h   (+ « Aucun agent connecté · Connecter »)
//   ConnectModal (+ veil, pill)   annotation-connect.h
//   SelectionFrame (V3)           annotation-frame.h    (the page frame's native copy during the capture)
//   EntryVeil (V3-B)              annotation-veil.h     (the entry's blue wash + rippling texture, 500 ms)
// No disk I/O, no dependency on the annotation store: plain structs in, callbacks out.
// Every surface is created at its first Show (Init only stores the context). Contract,
// states and motion values: design/annotations-ui.md.
#include "annotation-banner.h"
#include "annotation-connect.h"
#include "annotation-frame.h"
#include "annotation-veil.h"
