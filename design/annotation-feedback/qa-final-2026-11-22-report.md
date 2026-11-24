# QA finale — Mode Commentaire + Chat (agent Q)

Passe finale réduite du 22 novembre 2026, 18 h 35 – 18 h 50, sur **build/chat-preview 0.3.611 (805f349)**, arbre propre,
**0 avertissement** à la construction. Vraie fenêtre (SendInput, captures d'écran), zone de page 1470 × 632–666 px CSS, dpr 1,25,
faux agent démarré en mode « auto » comme le vrai Claude, `BROWSER_ANNOTATIONS_TEST=1`, configurations des agents redirigées
sous `build/`, empreintes des vrais fichiers Claude / Codex identiques avant et après chaque suite, rien laissé ouvert.

**Verdict : pas « tout vert ».** Les parcours de l'utilisateur passent (37/39, les 2 échecs : un harnais corrigé, une ouverture
à 178 ms) ; il reste des défauts connus et attribués (ci-dessous), principalement l'ouverture parfois absente sur
`section.hero` après certaines séquences, des ouvertures entre 150 et 216 ms, et le suivi au défilement hors ±2 px.

## Résultats

| Suite | Build | Résultat | Preuve |
|---|---|---|---|
| Parcours réels R1–R48 + revue (39) | 0.3.611 | **37/39**, 389 vérifications réussies | `build/chat-qa/final/runs/journeys.log`, `journeys/<nom>/` |
| comment-v5 (R39, R40, R41) | 0.3.611 | 20/25 — R40 et R41 verts, R39 hors tolérance | `runs/comment-v5-final.log` |
| Monkey graine 1, actions 0–159 | 0.3.611 | 160 actions, 160 vérifications d'invariants, 19 échecs | `monkey-1/report.json`, `echec-NN.json/.png` |
| Monkey graines 1, 2, 3 complètes (320) | 0.3.561 | 3 × 320 actions ; défauts corrigés depuis (mode, panneau inerte, envoi après erreur, file) | `runs/monkey-seed-*.log` (passes précédentes) |
| annotations-ui, smoke (test.ps1) | 0.3.561 | **verts** | `build/chat-qa/suites/` |
| i18n, unitaires, copies, ACP, panneau, faux agent 21/21 | 0.3.530–0.3.536 | **verts** | `runs/` |
| annotations, raccourcis, accessibilité, annotation-settings, split-view, ui, design-system, elements-integration, profiles(-ui, -store), bookmarks-ui, history, downloads, audio, popups, navigation-errors, password-vault, browser-import, status-bubble, tab-drag, hover-favicon, e2e-pin | 0.3.530–0.3.541 | **verts** | `build/chat-qa/suites/` |
| sandbox | 0.3.541 | échec **préexistant, identique sur main** (« DNS: search, never auto retry », réseau) | `suites/sandbox.log` |
| Anciennes vraies fenêtres (v1, v12, comment, v2, v3, v4b) adaptées | — | adaptées (liste ci-dessous), **pas rejouées** sur 0.3.611 faute de temps | — |

## Défauts restants (réels, attribués)

1. **[H1] Clic sans bulle** (monkey graine 1, 0.3.611) : clic sur `section.hero` jamais visible en 3 s — #116 juste après
   « autre application puis retour » pendant un tour avec autorisation (l'ancienne bulle de `div.hero-visual` reste à
   l'écran), #144 / #146 après « Commentaire puis clic à 50 ms », #159 `a.btn-primary`.
   Rejouer : `CHAT_QA_BUILD=chat-preview python tests/chat/final/monkey.py --seed 1 --until 116`.
2. **[P/H1] Ouvertures lentes** : 151–216 ms (cible < 150) dans le monkey (h2.section-title, h1.hero-title,
   div.hero-visual), 178 ms dans le parcours M5 ; la plupart des ouvertures des parcours sont sous 150 ms.
3. **[H1] R39, suivi au défilement** (comment-v5, image par image, tolérance ±2 px) : molette lente 10 px max
   (10/65 images), bords 9 et 16 px, molette rapide 62 px (5/16), clavier 70 px (3/77) ; barre de défilement verte.
   Monkey (pendant le défilement, tolérance 16 px CSS) : 17,6 et 25,6 px. Avant les correctifs : 41 à 143 px.
4. **[H1] Placement / redimensionnement** (monkey) : #82 bulle de h2.section-title déplacée après un menu ; #110 après un
   redimensionnement de la zone à 1458 × 444, la saisie sort de la surface (variante de M15).

Corrigés et vérifiés pendant la passe (extraits) : distance Paper +12 / −4 (r44, r06, r47), panneau inerte après la
bascule, mode / modèle / effort gardés entre panneau et bulle, envoi après une erreur, message en file visible dans sa
bulle, R45 à 100 / 125 / 150 %, R31, M4 bulle et panneau, M15 (hauteur normale), clic qui ramène le navigateur, C1 (avec
préchauffage retardé), C2, C3 (dont l'historique du lanceur), E1–E4, M1, M2, M5, M6, M11, M16, R42, R46, R47.

## Ce qui a été construit (fichiers de Q)

- `tests/chat/final/qa_final.py` — vraies fenêtres, SendInput, invariants du plan vérifiés après chaque action (bulle
  entière, < 150 ms, bonne conversation, collée à son élément, région = surface, jamais vide ni grisée, distance Paper,
  fenêtre visible ⇔ ouverte, mode / modèle / effort stables, marques après envoi, un seul contour, aucune erreur JS ni
  exception, UIA complet, message visible là où il est parti, bulle ouverte tant que l'utilisateur ne la ferme pas,
  suivi pendant le défilement). Un échec = capture + fin du journal + graine + pas pour rejouer.
- `tests/chat/final/journeys.py` — 39 parcours (`--list`), `tests/chat/final/monkey.py` — monkey reproductible par graine
  (`--seed`, `--until`, `--dry`), `tests/chat/final/run_final.py`, `fake_agent_scenarios.py`, `uia_server.ps1`.
- `tests/acp/fake_agent.cpp` — nouveaux scénarios seulement : `--start-mode`, `--new-delay`, `vrai-*` (tour réaliste en
  français, Markdown long, adresses nues, code, autorisation, surcharge 529, lenteur, outil en échec, agent sourd).
  Anciens scénarios inchangés (suite ACP verte).

## Attentes changées dans les anciennes suites (accord du coordinateur), chacune commentée « final QA »

| Suite | Attente avant | Attente maintenant | Décision |
|---|---|---|---|
| chat_qa_v12 | menu des modes V1.4 (Demander, Auto, Planifier, Autonome) | 4 lignes Paper 14RK-0 | R4 |
| chat_qa_v12 | bouton du mode avec texte « Auto » | nom accessible « Mode : Auto », icône seule | R21 |
| chat_qa_v12, chat_qa run | « a click on an element opens the chat » / « panel ready » après le bouton | « panel in place » accepté, dossier et site trouvés à la préparation | O1 (panneau préparé) |
| chat_qa_v12 | classe du cadre « x h » | « x f » | R14 / R45 |
| chat_qa_comment | « à côté » mesuré sur la fenêtre | sur la région de la bulle | R19 / R41 (marge transparente) |
| chat_qa_comment | cadre + icône et « En attente » bulle ouverte | bulle fermée | R2 / R27 |
| chat_qa_comment_v3, v4b | ouverture < 100 ms | < 150 ms | plan final |
| chat_qa_comment_v2 | infobulle par CDP ; R9 panneau par la 1re page | vrai pointeur ; page du panneau + clavier | M10 ; harnais |
| chat_qa run | rechargement obligatoire ; 0 page de chat au repos | fondu OU glissement ; 0 page de panneau (la page préparée de la bulle peut rester) | R36 ; R18 |
| chat_qa_comment_v5 | clavier après un clic sur la bordure droite | Ctrl+clic sur du texte | harnais (pas de vraie barre) |

Aucune attente n'a été changée pour masquer un défaut (R39 reste mesuré à ±2 px).
