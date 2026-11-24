# Mode Focus (branche `VALXXII/mode-focus-2`) — tournée de nuit

Détail technique : `design/focus-mode-report.md`, sections « Port to main », « With the annotation mode » et « #127 ».
Tout ce qui se fait sans fenêtre est vert (focus_mode_test 226, drawer_corners_test 46, split_ui_test avec la section
Annotation + Focus, ui_test, settings_test, shortcuts_test, barrière i18n 0, compilation complète sans avertissement).
Reste ce qui prend l'écran. À faire seul sur la machine, session ouverte, environ 40 min.

1. **Build dev** dans le dossier de travail de la branche (copies réelles de `tools/w64devkit`, `deps/webview2`,
   `deps/thorvg-1.1.3`, `build/lottie-backend`, `assets/extensions`, jamais de jonctions) :
   `python tools/release/build.py --out build/chat-preview` (≈ 6 min ; fait aussi `build/chat-preview-test`, l'agent factice).
2. **`focus_qa.py --full` en 3 configurations** (32 contrôles chacune : les 31 d'avant + le bas des tiroirs à 4 DIP) :
   - `python design/focus-mode-evidence/qa/focus_qa.py --app build/chat-preview --out build/focus-qa-125 --evidence build/focus-qa-125/evidence --full`
   - `python design/focus-mode-evidence/qa/focus_qa.py --app build/chat-preview --out build/focus-qa-100 --evidence build/focus-qa-100/evidence --full --monitor secondary --y 1100`
   - `python design/focus-mode-evidence/qa/focus_qa.py --app build/chat-preview --out build/focus-qa-125-band --evidence build/focus-qa-125-band/evidence --full --band`
3. **Contrôle à l'œil des tiroirs** (≈ 5 min), en mode normal puis en Mode Focus (Ctrl+Maj+F) : ouvrir et fermer
   Réglages, Historique, Favoris (Ctrl+Maj+O) et Téléchargements, y compris « Afficher tout » depuis la petite fenêtre.
   - Aucun éclair à l'ouverture ni à la fermeture.
   - En Focus : les deux coins du haut arrondis (6) et lisses au repos ; bord droit à 4 du bord de la fenêtre, bas à 4
     du bas de la fenêtre. Pendant le glissement (0,2 s), le coin gauche peut paraître un peu crénelé : c'est prévu.
4. **`test.ps1`** (smoke, 6–7 min).
5. **Parcours Annotation + Focus** : `python tests/focus/annotation_focus_journey.py --tab-layout top`
   → `build/chat-qa/focus-journey-top/report.json` et les captures. Il vérifie : bandeau en haut, chat en carte à droite,
   chat utilisable, panneau révélé à 36 au-dessus de la page, tiroir à 124 par-dessus le chat, puis la sortie de Focus
   sans saut ; et l'ordre inverse (Focus puis Annotation). Quand le lot 1 aura ajouté l'entrée `focus-annotation` au
   manifeste, il tourne aussi dans `tests/annotation-general/run.py --night`.

Quand #127 sera fusionné : `--tab-layout side` et `compact` (à brancher, voir le rapport, section « #127 »).
