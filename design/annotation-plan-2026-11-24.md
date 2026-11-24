# Mode annotation — inventaire des bugs, plan de correction et test général (24 novembre 2026)

État lu : `main` au commit `86697e0` (dernière version publiée : **0.3.769**). Aucun fichier du produit n'est modifié par ce
document. Rien n'a été lancé en vraie fenêtre ; seules des suites sans fenêtre ont tourné (résultats au §6).

## 0. En bref

- **Tous les bugs connus ont été recensés : 111 éléments.** 35 de la revue du 21 novembre, 49 retours de test (R1 à R48 +
  R39b), 18 retours plus anciens (annotations V2 et chat V1.2), 4 défauts du rapport de QA du 22 novembre, 3 bugs vus après
  la sortie, #129 et #130.
- **12 sujets sont encore ouverts** (§2.2). Les deux plus gênants sont #130 (boutons Refuser / Autoriser hors de la fenêtre)
  et #129 (menu des modèles vide), déjà en cours de correction ailleurs. Viennent ensuite : un clic qui n'ouvre pas de bulle
  dans certains enchaînements, la bulle qui suit mal au défilement, la bulle mal placée après un redimensionnement.
- **4 bugs sont revenus après avoir été corrigés** (§2.3). Point commun : le correctif a été validé par un test ciblé, pas
  par la série complète.
- **Pourquoi les bugs reviennent.** La série complète en vraie fenêtre n'a jamais tourné en entier sur la version publiée.
  Le 22 novembre, 7 des 11 anciennes suites échouaient : 6 ont été « adaptées » puis pas rejouées, la 7ᵉ échoue encore
  (défilement). Depuis, 4 mises à jour ont touché le chat (0.3.690, 0.3.710, 0.3.715, plus la fusion de sortie) avec des
  vérifications réduites.
- **9 des 35 bugs de la revue n'ont qu'une preuve faible** : ils ne sont prouvés que par un test qui relit le code (il
  vérifie que la ligne existe, pas que ça marche à l'écran).
- **Le plan** (§3) : d'abord un filet de sécurité (lot 0 : le test général, sans changer le produit), puis 5 lots de
  corrections. Chaque lot doit passer le test général en entier avant de sortir.
- **Le test général** (§4) : une partie sans fenêtre (≈ 15 min, faisable en journée) et une partie en vraie fenêtre pour la
  nuit (≈ 1 h 45 à 2 h 15). Il refuse de dire « vert » si une seule suite est sautée, filtrée ou relancée.
- **Décisions** (§5) : les 6 sont prises (24 novembre). #130 l'emporte sur R29, variante C. Annoter reste allumé sur les
  fichiers locaux et en fenêtre privée. Le « lot » est retiré en entier. Un essai réel par agent est autorisé. 150 ms et
  ± 2 px sont des objectifs, pas des conditions de sortie. Builder code l'extinction hors projet local.
- **Lot 0 fait** (§7) : documents de retours commités, diagnostics fusionnés, test général écrit dans
  `tests/annotation-general/`.

## 1. Sources et versions

| Source | Où |
|---|---|
| Revue indépendante du 21/11 (les « 35 bugs » : C1–C3, E1–E5, M1–M16, F1–F11) | `design/chat-final-review.md` |
| Retours de test R1 à R48, décisions D1 à D8 | `design/annotation-feedback/chat-comment-v2-retours.md`, `chat-comment-mode-brief.md`, `chat-v12-model-picker.md` |
| Retours annotations V2 (11 points) et chat V1.2 (7 points) | `design/annotation-feedback/annotations-v2-retours.md` |
| Plan de la version finale et invariants | `design/annotation-feedback/chat-comment-final-plan.md` |
| Rapport de QA du 22/11 (build 0.3.611, commit `805f349`) | `design/annotation-feedback/qa-final-2026-11-22-report.md`, `qa-final-2026-11-22-defauts-passe1.md` ; les journaux (`journeys.json`, `runs/`) restent dans le worktree `annotations-chat`, non commités |
| Validations | `design/chat-v1-validation.md`, `design/annotations-v2-validation.md` |
| Tickets | #125, #128, #129, #130 ; PR #99, #100, #106, #111, #113 |
| Code | `src/chat-*.h`, `src/annotation-*.h`, `assets/chat-panel/`, `tests/chat*`, `tests/annotations*`, `tests/acp` |

Ces documents n'étaient dans aucun dépôt. Ils sont commités depuis le lot 0 dans `design/annotation-feedback/` (chemins
personnels remplacés par des chemins neutres, contenu inchangé).

Versions publiques qui comptent pour le mode annotation (dépôt `browser-releases`) :

| Version | Date | Contenu |
|---|---|---|
| 0.3.294 | 15/11 | Annotations V1 à V3 : note, historique, lanceur, pont MCP, connexion Claude / Codex (PR #70, #73, #75) |
| **0.3.630** | 22/11 | Chat intégré, mode Commentaire (bulle), les correctifs de la revue et de R1–R48 (PR #99, #100) |
| 0.3.690 | 23/11 | Correctifs de l'audit sécurité / performance, dont 7 commits dans le chat (PR #106) |
| 0.3.710 | 23/11 | Bouton modèle de la bulle (PR #111) |
| 0.3.715 | 23/11 | Dossier du projet, compteur de fichiers, bandeau « Préparation » (PR #113) |
| 0.3.769 | 23/11 | Dernière version ; rien de nouveau dans le mode annotation depuis 0.3.715 |

## 2. Inventaire

### 2.1 Synthèse

| Famille | Total | Corrigés et publiés | Revenus | Ouverts |
|---|---|---|---|---|
| Revue du 21/11 | 35 | 32 | 0 | 3 avec une variante ouverte (E4, M15, F11) |
| Retours R1–R48 (+ R39b) | 49 | 44 | 3 (R2, R13, R47) | 2 (R39, R39b) ; R29 remplacé par #130 (décision du 24/11) |
| Annotations V2 (11) et chat V1.2 (7) | 18 | 17 | 0 | 1 à prouver avec un vrai agent |
| QA du 22/11 | 4 | 0 | — | 4 |
| Vus après la sortie (dossier, compteur, bandeau) | 3 | 3 | 1 (dossier) | 0 |
| Tickets | 2 | 0 | — | 2 (#129, #130, en cours) |

Niveaux de preuve utilisés dans les tableaux :

- **Parcours** : rejoué en vraie fenêtre le 22/11 sur 0.3.611 (`tests/chat/final/journeys.py`, 37 parcours verts sur 39).
  Jamais rejoué en entier depuis, alors que le code a changé 4 fois.
- **Page** : test du panneau sans fenêtre (`tests/chat-panel/run.py`), **repassé aujourd'hui sur main : 802 / 802**.
- **Unité** : `tests/chat/chat_units_test.cpp`, **repassé aujourd'hui : 486 vérifications**.
- **Contrat** : `tests/chat/host_contract_test.py`, **110 / 110 aujourd'hui**. Il relit le code source. Il prouve que le
  correctif est écrit, pas que le comportement est bon.

### 2.2 Bugs ouverts

| # | Bug | Zone | Depuis | Preuve | État |
|---|---|---|---|---|---|
| O1 | **#130** — la bulle dépasse le bas de la fenêtre ; Refuser / Autoriser inaccessibles | bulle, autorisations | 0.3.630 | La hauteur donnée à la bulle n'est jamais limitée à la place sous son bord fixe (`src/chat-comment.h:261-263`, décision R29). Les deux seuls filets : faire défiler la page du site (`:716-727`), sans effet si la page ne peut pas défiler ; un décalage unique en fin de document (`:199-212`, `endAsked`), calculé avant l'arrivée de la carte | En cours sur `VALXXII/fix-129-bubble-model-menu` |
| O2 | **#129** — le menu des modèles ne montre rien dans la bulle | menu des modèles | 0.3.630 ; visible depuis 0.3.710 | Dans la bulle, les fournisseurs sans liste de modèles sont masqués (`assets/chat-panel/panel.js:903`, règle R13). Profil neuf ou agent pas encore démarré = aucune liste = menu vide. Avant 0.3.710 le bouton disait « Claude Code », on ne cherchait pas un modèle | En cours, même branche |
| O3 | **Clic sans bulle** (« H1 » du rapport de QA) : en Commentaire, un clic sur `section.hero` ou `a.btn-primary` n'ouvre rien, ou ouvre l'ancienne note | sélection, bulle | ≤ 0.3.611 ; sorti en connaissance de cause (PR #100) | Rapport QA 22/11, défaut 1 : monkey graine 1, pas #116, #144, #146, #159. Diagnostic `c419d92` écrit mais **jamais fusionné**. Trois pistes lues dans le code : le clic est refusé parce que la sélection est en pause (`src/chat-host-ui.h:156`), d'où la note ; 400 ms sans effet après une fermeture (`src/chat-comment.h:1025`, `:1107`) ; bulle retenue en attendant une mesure de la page (`:673`) | Ouvert, cause non prouvée |
| O4 | **R39 / R39b** — la bulle suit son élément en retard au défilement, et sa coupe est visible au bord de la zone de page | bulle | régression du 20/11 (correctif de R37), jamais fermée | QA 22/11, défaut 3 : molette 10 px, molette rapide 62 px, clavier 70 px, bords 9 et 16 px, pour ± 2 px visés. La fenêtre est déplacée « tout de suite », sans attendre l'image de la page (`src/chat-comment.h:351`, `FollowFrames` = 0) | Ouvert |
| O5 | **Bulle mal placée après un redimensionnement ou un menu** : la saisie sort de la bulle après une zone réduite à 1458 × 444 ; la bulle de `h2.section-title` bouge après un menu | bulle | ≤ 0.3.611 | QA 22/11, défaut 4 : monkey #110 (variante de M15) et #82 (variante de E4) | Ouvert |
| O6 | **Ouverture de la bulle en 151–216 ms** au lieu de 150 | bulle | ≤ 0.3.611 | QA 22/11, défaut 2 ; parcours `m5_bascule_puis_clic` : 178 ms | Ouvert, gêne faible. **Objectif, pas condition de sortie** (décision 5) |
| O7 | **Le « lot » (Traiter les annotations) à moitié retiré** : la suggestion a été supprimée (R24), le reste du code et ses tests sont toujours là ; des échecs du lot vus le 23/11 n'ont jamais été confirmés | chat | 20/11 (R24) | `src/chat-host-batch.h`, 44 mentions dans `panel.js`, `tests/chat/chat_qa_run.py:509-547` ; PR #113 : « batch-panel failures that look unrelated », vérification arrêtée | **Décision 3 : à retirer en entier** (lot 5) |
| O8 | **Chemin « profil neuf » jamais testé** : pas de cache des modèles, agent pas installé, pas connecté, Node.js à télécharger | menu des modèles, connexion | 0.3.630 | C'est la cause commune du bug « Claude Code » (PR #111) et de #129 : tous les tests partent d'un faux agent qui donne ses modèles | Trou de test |
| O9 | **Vrais agents jamais prouvés** : Claude lit une annotation mais ne la « prend » ni ne la « termine » ; Codex n'a jamais eu d'essai réel dans le chat | MCP, connexion | V2 (14/11) | `design/annotations-v2-validation.md` §6 : « Que Claude appelle désormais claim / resolve n'est pas démontré » ; `design/chat-v1-validation.md` : « Codex n'a pas eu d'essai réel » | À prouver |
| O10 | **Autorisations après 0.3.690** : la carte ne prend plus le focus sur « Autoriser » et attend 600 ms avant d'accepter une touche (SEC-23) ; jamais rejoué en vraie fenêtre en entier | autorisations | 0.3.690 (commit `c9e91cf`) | `assets/chat-panel/panel.js:1149-1195`, `:1512-1516` ; PR #106 : seuls 3 parcours rejoués (r02, r38, r47) | À revalider |
| O11 | **Diagnostic resté dans la version publiée** : tant qu'une bulle est ouverte, un fil envoie un message toutes les 10 ms à la fenêtre | bulle (performance) | 0.3.630 | `src/chat-comment.h:180-190` (`UiWatchStart`), lancé à chaque clic (`:1037`), sans condition de mode test | Ouvert, gêne faible |
| O12 | **Fichiers locaux (`file://`) et fenêtre privée** : le chat y est coupé et l'ancienne note s'ouvre à la place | local / en ligne | V1 du chat | `src/chat-host-ui.h:150-156` (`AskAvailable`, `CommentFirst`) | **Décision 2 : Annoter y reste allumé.** Le comportement d'aujourd'hui est gardé ; il reste à le tester (lot 3) |

Aussi ouverts, côté tests et non côté produit :

- 7 anciennes suites en vraie fenêtre sur 11 échouaient le 22/11 à 13 h 44 (`chat_qa run`, `comment`, `v12`,
  `comment-v2`, `v3`, `v4b`, `v5` ; journal `runs/run_final-B.log`). Six ont été adaptées puis **pas rejouées** (rapport
  QA : « pas rejouées sur 0.3.611 faute de temps »). `comment-v5` a été rejouée : 20 / 25, en échec sur R39.
- Le correctif du test R7 (commit `49af0f5`) n'est pas fusionné : ce parcours échoue encore sur main pour une raison de test.
- Trois points « à vérifier » de la première passe n'ont pas de conclusion : image du haut de la bulle qui change à
  l'ouverture d'un menu (R19), F5 avant le premier clic (M16, outil de test douteux), « Arrêter » après une erreur de surcharge.
- Les suites en vraie fenêtre ne tournent pas dans un Job Object (`tests/chat/chat_qa.py`) : un arrêt brutal peut laisser
  un navigateur de test ouvert.
- Le test `sandbox` (page d'erreur DNS) échoue sur main depuis avant le chat. Il n'est pas du mode annotation.

### 2.3 Corrigés, puis revenus

| Bug | Corrigé | Revenu | Cause | État aujourd'hui |
|---|---|---|---|---|
| **R13** — le bas de la bulle montre « Claude Code » au lieu du modèle | 20/11, parcours `r42_modele` et `r20_r30_r01_bulle_forme` | 0.3.630, sur profil neuf | Le cache des listes de modèles n'était écrit et relu qu'en version d'essai, pas dans le dossier du profil (PR #111) | Corrigé en 0.3.710 (`c2d507e`). Son voisin #129 reste ouvert |
| **R47** — Refuser / Autoriser coupés en bas de la bulle | 21/11, parcours `r47_autorisation_defilement`, revu le 23/11 (PR #106) | 0.3.769, ticket #130 | Le correctif fait défiler la page du site. Élément bas dans une page qui ne défile pas : plus de filet | Ouvert (O1) |
| **R2** — cadre et icône « conversation » dès le clic | 18/11 | 20/11 (R32) | Régression pendant le développement | Corrigé en 0.3.630, parcours `r02_r32_marque_apres_envoi`, revu le 23/11 |
| **Dossier du projet trouvé tout seul** | chat V1 (18/11) | 0.3.690–0.3.710 : mauvais dossier | La détection faisait confiance au dossier de lancement du serveur | Corrigé en 0.3.715 (`fb45f8a`, `7f8d612`), 486 tests unitaires aujourd'hui ; grande suite arrêtée avant la fin |

Deux enchaînements du même genre, à surveiller : le correctif de R37 (inspecteur en retard) a créé R39 (bulle en retard,
toujours ouvert) ; les correctifs de R19 / R29 ont créé E4, dont il reste la variante O5.

### 2.4 Revue du 21/11 : les 35

Les 35 ont un correctif dans le code de main et sont sortis en **0.3.630**. Ce qui change, c'est la solidité de la preuve.

| # | Sujet | Correctif (main) | Preuve | Remarque |
|---|---|---|---|---|
| C1 | À l'ouverture, la bulle n'a ni fil ni état | `chat-host-ui.h:864-869` | Parcours ×2, contrat | |
| C2 | Message envoyé dans la mauvaise conversation | `chat-comment.h:921-929`, `chat-host-ui.h:742` | Parcours, contrat | |
| C3 | Élément introuvable = bulle fermée | `chat-comment.h:758-773`, `:1199` | Parcours ×2, contrat | |
| E1 | Clic dans un iframe ou un composant web | `annotation-script.h:393`, `chat-comment.h:1061-1068` | Parcours, unité | |
| E2 | Modèle choisi perdu au 2ᵉ élément | `chat-host-turn.h:179` | Parcours, unité | |
| E3 | Choix fait dans B appliqué à A | `chat-host-ui.h:490`, `:931`, `:954` | Parcours, unité | |
| E4 | La bulle saute après défilement puis menu | `chat-bubble.h:148-175`, `chat-comment.h:276-285` | Parcours, unité | **Variante ouverte : O5** |
| E5 | Conversation lue et ouverte deux fois | `chat-comment.h:829-871` | Contrat seul | Preuve faible |
| M1 | « Arrêter » sans réponse de l'agent | `chat-host-turn.h:845`, `chat-host-ui.h:566` | Parcours | |
| M2 | Ancien fil visible à l'ouverture suivante | `chat-comment.h:626-628`, `:706` | Parcours, page | |
| M3 | Marque « en cours » qui reste | `chat-host-ui.h:49-59`, `chat-transition.h:192` | Unité (`page_v4_test`), contrat | Pas de parcours |
| M4 | Clavier perdu après une autre application | `chat-comment.h:410-431`, `:474` | Parcours ×2 | |
| M5 | Clic pendant la sortie du panneau | `chat-comment.h:590`, `chat-host-ui.h:689` | Parcours (2 / 3) | 3ᵉ essai : ouverture en 178 ms (O6) |
| M6 | Clic sans envoi qui marque l'élément | `chat-comment.h:685-696`, `:1163` | Parcours | |
| M7 | Réessayer / diff pris dans l'autre conversation | `chat-host-turn.h:895-903`, `chat-host-ui.h:833` | Contrat seul | Preuve faible |
| M8 | Bouton du mode absent au premier clic | `chat-host-ui.h:604-627` | Contrat seul | Preuve faible ; lié à O8 |
| M9 | Mode remis à « Demander » à chaque élément | `chat-host-turn.h:145` | Parcours (`e2_…`), unité | Décision : le mode est gardé |
| M10 | Infobulle qui reste dans la bulle | `chat-host-ui.h:545-546` | Vraie fenêtre (`comment-v2`), pas rejouée | |
| M11 | « Travaille… » sans fin après fermeture | `chat-host-ui.h:1044` | Parcours, page | |
| M12 | Enregistrements simultanés | `chat-store.h:204` | Unité | |
| M13 | Tour qui ne démarre jamais | `chat-host-turn.h:287-288` | Contrat seul | Preuve faible |
| M14 | Message retenu jamais envoyé | `chat-comment.h:1133-1135`, `chat-host-ui.h:565` | Contrat seul | Preuve faible |
| M15 | Saisie coupée après un redimensionnement | `chat-comment.h:234-243`, `:257-263` | Parcours, unité | **Variante ouverte : O5 ; lié à O1** |
| M16 | F5 avant le premier clic quitte le mode | `chat-host-ui.h:723` | Parcours | Outil de test à revérifier |
| F1 | Titre Markdown qui fige la page | `markdown.js:8-10` | Page | |
| F2 | Lien `file:///` mort | `markdown.js:23` | Page | |
| F3 | « Nouveau chat » d'une carte d'erreur | `chat-host-ui.h:1014` | Contrat seul | Preuve faible |
| F4 | Bulle fermée lisible par un lecteur d'écran | `panel.js:3041-3044` | Page | |
| F5 | Mémoire jamais libérée | `chat-comment.h:685-691` | Page, contrat | |
| F6 | Texte tapé perdu à la fermeture | `panel.js:2944`, `chat-comment.h:153-155` | Page | |
| F7 | Bulle « ouverte » sans WebView | `chat-host-ui.h:635` | Contrat seul | Preuve faible |
| F8 | Bulle qui s'attache sur une autre page | `chat-comment.h:79-83` | Contrat seul | Preuve faible |
| F9 | « Voir » sans défiler jusqu'à l'élément | `chat-comment.h:951` | Contrat seul | Preuve faible |
| F10 | « Refuser » qui ne fait rien | `panel.js:1188`, `:1511` | Page | |
| F11 | Position de défilement appliquée trop tôt | `chat-comment.h:323-325`, `:791` | Unité (modèle) | **Remplacé par R39 : O4** |

### 2.5 Retours R1 à R48

Tous sortis en 0.3.630 sauf mention. « Parcours » = nom dans `tests/chat/final/journeys.py`.

| # | Sujet | État | Preuve |
|---|---|---|---|
| R1 | Coins de la bulle | Corrigé | Parcours `r20_r30_r01_bulle_forme` ; `comment-v2` (pas rejouée) |
| R2 | Marque seulement après un envoi | Corrigé, revenu une fois (R32) | Parcours `r02_r32_marque_apres_envoi` |
| R3 | Menus au-dessus de la bulle, non coupés | Corrigé | `comment-v2` (pas rejouée) ; page |
| R4 | Menu des 4 modes | Corrigé | Page |
| R5 | « + » sans dossier | Corrigé | Page |
| R6 | Clic sur un élément qui a une conversation | Corrigé | Parcours `r06_…`, `abab` |
| R7 | Pas de puce sous le message | Corrigé | Page ; parcours en échec pour une raison de test (`49af0f5` non fusionné) |
| R8 | Phrase d'annonce avant le travail | Corrigé | Page ; parcours `r43_r48_…` |
| R9 | Tour très lent puis bloqué | Corrigé | Parcours `r09_…`, `r47_…` |
| R10 | Masque du bandeau | Corrigé | `tests/annotations-ui` (fenêtre, pas rejoué aujourd'hui) |
| R11 | Double-clic sur le titre = renommer | Corrigé | Page |
| R12 | Logo du fournisseur | Corrigé | Page |
| R13 | Modèle au lieu de « Claude Code » | **Revenu**, corrigé en 0.3.710 | §2.3 ; voisin #129 ouvert |
| R14, R33, R45 | Étiquettes identiques au survol et à la sélection | Corrigé | Parcours `r14_r45_etiquettes` (100 / 125 / 150 %) |
| R15 | 2 px du bord de la fenêtre | Corrigé | Parcours `r07_r15_r19_r22_details` |
| R16, R27, R37 | Inspecteur padding / gap, un seul contour, collé au défilement | Corrigé | `tests/annotations-ui/page_v4.py`, `comment-v4b` (pas rejouées) |
| R17 | Une carte par message | Corrigé | Page |
| R18 | Pas d'image intermédiaire à l'ouverture | Corrigé ; cible passée de 100 à 150 ms | Chaque parcours ; reste O6 |
| R19 | Rien ne saute au clic dans la bulle | Corrigé | Parcours `r07_…` ; reste O5 et un point « à vérifier » |
| R20, R21, R22, R30 | 320 px, icône seule, « + » direct, entrée 250 ms | Corrigé | Page ; parcours `r20_…` |
| R23, R24 | Chat vide en bas, suggestion retirée | Corrigé | Page ; parcours `r11_r23_r24_panneau` ; reste O7 |
| R25 | Bandeau qui glisse | Corrigé | `comment-v4b` (pas rejouée) |
| R26 | Survol d'une puce = la page défile | Corrigé | Parcours `r26_survol_puce` |
| R28, R46 | Bas de la saisie stable, mode jamais sur « Auto » tout seul | Corrigé | Parcours `r46_coller_mode_stable` ; reste O8 |
| R29 | Position figée une fois posée | **Remplacé le 24/11** : #130 l'emporte, la bulle reste entière dans la fenêtre | Code actuel encore selon R29 (`src/chat-comment.h:261-263`) ; nouvelles attentes au §4.4 |
| R31 | Ctrl maintenu = site normal | Corrigé | Parcours `r31_ctrl_maintenu` |
| R32 | Régression de R2 | Corrigé | Parcours `r02_…` |
| R34, R35 | Bascule Commentaire / Chat, brouillons séparés | Corrigé | Parcours `r34_r35_bascules_mode` |
| R36, R38, R40 | Fondu après modification, diff en place, poignée | Corrigé | Parcours `r38_r40_diff_poignee` ; `comment-v5` |
| **R39, R39b** | Bulle en retard au défilement, coupe visible | **Ouvert** | O4 |
| R41 | Menus qui se transforment sans éclair | Corrigé | `comment-v5` (vert le 22/11) |
| R42 | Changer de modèle sans confirmation | Corrigé | Parcours `r42_modele` |
| R43, R48 | Lignes de suivi, langue de l'utilisateur, plus de bloc bleu | Corrigé | Parcours `r43_r48_…`, suite `r43` |
| R44 | Grand élément : bulle entière à l'ouverture | Corrigé à l'ouverture | Parcours `r44_…` ; la bulle peut dépasser ensuite : O1 |
| R47 | Autorisation coupée | **Revenu** | O1 |

### 2.6 Retours plus anciens et bugs vus après la sortie

D'après `design/annotations-v2-validation.md` et `design/chat-v1-validation.md`. Non revérifiés dans le code pour ce document.

| Retour | État | Preuve |
|---|---|---|
| V2-1 « Redémarrez Claude » après la connexion | Corrigé (0.3.294) | Validation V3-A, planche 5.6 |
| V2-2, V2-3 Flashs à la capture et dans l'interface | Corrigé | Validation V3-A §4 |
| V2-4 Curseur viseur | Corrigé | Validation V3-B |
| V2-5 L'historique ne dit pas que l'agent s'en occupe | Corrigé côté navigateur ; **comportement du vrai agent non prouvé** | O9 |
| V2-6 Lanceur seulement en mode annotation | Corrigé | Validation V3-A, planche 5.8 |
| V2-7 Connexion en un clic pour Claude Code | Remplacé par le chat intégré | PR #99 |
| V2-8, V2-9, V2-10 Espacements, entrée animée, trame vivante | Corrigé | Validation V3-B, planches 5.1 à 5.3 |
| V2-11 Aperçu « Ce que l'agent verra » coupé | Corrigé ; ne concerne que l'ancienne note | Validation V3-A, planche 5.7 |
| V1.2-1 à V1.2-7 (texte de la note perdu, effort, puce dossier, jauge, cadre décalé, curseur, clic = chat) | Corrigés (0.3.630) | `tests/chat/chat_qa_v12.py`, en échec puis adaptée le 22/11, pas rejouée |
| Mauvais dossier de projet | Corrigé en 0.3.715 | §2.3 |
| Compteur et « Tout annuler » comptaient les fichiers d'autres programmes | Corrigé en 0.3.715 | `fb45f8a`, `tests/snapshots` (vert aujourd'hui) |
| Bandeau gris « Préparation » vide | Corrigé en 0.3.715 | `b6d5dd3` ; essai en vraie fenêtre d'un seul cas |

### 2.7 Ce que #125 retire

Décision du propriétaire sur #125 : « Annoter sera éteint hors projets locaux ». Aujourd'hui le mode s'active partout ; hors site
local le chat est coupé et l'ancienne note native prend le relais (`src/chat-host-ui.h:150-164`).

**Aucun effort à dépenser sur** (ces chemins disparaissent avec #125) :

- la note native sur un site en ligne : placement, coins, pastille « Capture impossible », aperçu « Ce que l'agent
  verra » (V2-11) ;
- la capture d'un élément sur un site quelconque (iframes d'un autre site, pages très lourdes, sites qui bloquent le script) ;
- le lanceur et l'historique sur un site en ligne, le regroupement par site en ligne ;
- le panneau de chat en état « indisponible : site pas local » (`src/chat-host-ui.h:684-685`) et ses tests ;
- Ctrl maintenu (R31) et le curseur viseur face aux raccourcis de sites en ligne.

**Reste nécessaire après #125** : tout le parcours sur un projet local (bulle, chat, autorisations, menus) ; le pont MCP
pour un agent extérieur (une annotation gardée par Ctrl + Entrée part toujours dans le magasin) ; la connexion des agents ;
et le nouveau bouton éteint avec son explication.

Je n'ai trouvé dans les sources **aucun bug ouvert qui n'existe que sur un site en ligne**. #125 retire donc des chemins à
tester, pas des lignes de l'inventaire. Décision 2 : Annoter reste allumé sur les fichiers locaux et en fenêtre privée ;
la note native n'est donc pas retirée, elle reste le chemin de ces deux cas et doit rester testée.

## 3. Plan

### 3.1 Ordre de gravité

1. **O1** (#130) : on ne peut plus répondre à l'agent, le tour reste bloqué.
2. **O2** (#129) : impossible de choisir un modèle sur un profil neuf.
3. **O3** : un clic sans effet ; l'utilisateur croit que le mode est cassé.
4. **O5** : la saisie sort de la bulle après un redimensionnement.
5. **O10** : autorisations à revalider après le changement de sécurité.
6. **O8** : premier lancement d'un nouvel utilisateur, jamais testé.
7. **O4** : gêne visuelle à chaque défilement.
8. **O9** : vrais agents.
9. **O7** (retrait décidé), **O12** (reste allumé : tests à écrire).
10. **O6**, **O11** : finitions.

### 3.2 Par zone

| Zone | Bug | Correctif prévu | Test qui le prouve |
|---|---|---|---|
| Bulle | O1 | Suivre la branche en cours. Règle décidée le 24/11 : la bulle reste toujours entière dans la fenêtre (#130 remplace R29). **Variante C choisie** (planche « #130 Bulle sans place dessous · C remonte le long du côté (validée) », page Paper « MCP - Design UI UX »). Ses règles 1 à 4 partent sur la branche en cours ; la règle 5 (recalcul pendant le défilement) est au lot 4. Refaire le calcul à chaque nouvelle hauteur, pas une fois par ouverture | Attentes du §4.4. Nouveau parcours `o1_bulle_bas_de_fenetre` : élément en bas d'une page qui ne défile pas, autorisation, clic réel sur Autoriser et sur Refuser, à 100 / 125 / 150 % et 3 tailles de fenêtre. Test de propriété sans fenêtre sur `ChatBubble` : pour toute position et toute hauteur, la surface est dans la zone |
| Bulle | O5 | Rejouer monkey graine 1 jusqu'à #82 et #110, lire le journal, corriger le calcul de la place offerte après redimensionnement et le cadre gardé après un menu | Parcours `o5_redimensionnement_saisie` et `o5_menu_ne_bouge_pas` ; invariant « la saisie est dans la surface » après chaque action du monkey |
| Bulle | O4 + **règle 5 de #130** | Deux choses dans le même code de suivi. (a) Suivi : mesurer d'abord, image par image, les trois réglages déjà dans le code (tout de suite, une image plus tard, prédiction) ; si aucun ne tient ± 2 px, c'est une limite de la fenêtre séparée : proposer une alternative au propriétaire, sans l'appliquer. (b) Règle 5 de la variante C : la position de la bulle est recalculée **pendant** le défilement selon les règles 1 à 4, et la bulle rejoint sa nouvelle place par un glissement animé de 120 ms au plus, sans saut (mouvement réduit : question 6 du §5). Même chose quand la fenêtre change de taille | (a) `tests/chat/chat_qa_comment_v5.py` (`follow`, `edge_frames`), seuil inchangé tant que la bulle est à sa place normale. (b) Nouveau parcours `o1_regle5_defilement` : faire défiler jusqu'à ce que la place manque sous l'élément, puis revenir ; image par image : bulle entière à la fin de chaque glissement, glissement ≤ 120 ms, aucun saut, jamais sur l'élément ni à moins de 8 px du bandeau ; molette, barre et clavier |
| Bulle | O6 | Lire les temps par phase déjà écrits dans le journal (`src/chat-comment.h:681-683`), traiter la phase la plus lente | Mesure « ouverture < 150 ms » sur chaque parcours, **rapportée comme objectif** : elle ne fait pas échouer le test général (décision 5) |
| Bulle | O11 | Ne lancer le fil de surveillance qu'en mode test | Contrat + mesure du processeur au repos, bulle ouverte |
| Sélection | O3 | Fusionner le diagnostic `c419d92`, rejouer `monkey.py --seed 1 --until 116` (puis 144, 146, 159), lire la raison du refus, corriger. Règle visée : en Commentaire sur un projet local, un clic ouvre toujours une bulle, jamais l'ancienne note | 4 parcours `o3_…`, un par enchaînement ; invariant monkey « clic = bulle visible en 3 s » sur 3 graines |
| Menu des modèles | O2 | Suivre la branche en cours | Scénario de page « aucun modèle connu » ; parcours « profil neuf, clic sur le bouton modèle » |
| Menu des modèles, connexion | O8 | Pas de correctif connu : écrire les scénarios, corriger ce qu'ils trouvent | 4 parcours sur profil neuf : agent absent, agent pas connecté, Node.js à télécharger, cache vide puis liste reçue |
| Autorisations | O10 | Rien à corriger a priori | Parcours clavier : Tab, Entrée, Échap sur la carte, délai de 600 ms, touche maintenue, deux cartes de suite ; bulle et panneau |
| Chat | O7 | Décision 3 : retirer « Traiter les annotations ensemble » en entier — hôte (`src/chat-host-batch.h`, `src/chat-batch.h`, appels), page (`panel.js`, `panel.css`, `dev-host.js`), tests (`chat_qa_run.py` étapes 6.15 à 6.17, unités, scénarios de page), textes | Contrat : plus aucune mention du lot dans l'hôte ni dans la page ; test général vert sans les étapes du lot |
| Local / en ligne | #125, O12 | L'extinction d'Annoter hors projet local est codée par **Builder** sur `VALXXII/125-annotation-onboarding` (commit `a65f50b`, pas encore fusionné), dans `src/annotation-sites.h`, avec la même règle que le chat (`ChatLocal::Classify`). **Allumé** : `localhost`, `*.localhost`, `127.x`, `[::1]`, fichier local `file://`. **Éteint** : tout le reste, y compris une adresse du réseau local (192.168.x, nom de machine) et un partage réseau `file://serveur/…`. Une fenêtre privée suit la même règle (décision 2). Sur un site éteint : bouton Annoter en état indisponible, la note native ne s'ouvre plus. De notre côté, après la fusion de #125 : les tests, et O12 (protections du chat sur `file://` et en fenêtre privée, non touchées par Builder) | Parcours `local_en_ligne` : `localhost`, `127.0.0.1`, `*.localhost`, `[::1]` = allumé avec chat ; `file://` local = allumé avec la note ; fenêtre privée = même règle ; site en ligne, **adresse du réseau local**, faux localhost (`localhost.example.com`), partage réseau = éteint, aucune note. Deux tests en vraie fenêtre vont sur un site non local et attendent l'ancien état « site pas local » du chat : `tests/chat/chat_qa_run.py:600` et `tests/chat/chat_qa_v12.py:220` — à réécrire (le mode s'arrête). Unités de Builder : `tests/annotations/annotation_sites_test.cpp` |
| MCP, agents | O9 | Rien à corriger avant d'avoir vu | Sans fenêtre : annotation gardée, lue, prise, terminée par le client de test du pont (`tests/annotations`). En vrai (décision 4, accordée) : **un** essai avec Claude Code et **un** avec Codex, une petite demande chacun, sur une copie de test seulement, avec les comptes du propriétaire ; rien d'autre que cette copie n'est touché, empreintes des configurations vérifiées avant et après |

### 3.3 Lots

Les durées sont des **estimations**. Elles comptent le travail et une nuit de test par lot.

| Lot | Contenu | Estimation | Dépend de |
|---|---|---|---|
| **0 — Filet de sécurité** | **Fait le 24/11** (§7). Aucun changement de comportement du produit. Documents de retours commités, 2 commits de diagnostic fusionnés, test général écrit, option « sans console » du test ACP, tests de R29 mis à jour. Reste : la première nuit complète, qui donnera la vraie liste des échecs | 1 jour + 1 nuit | Fusion de `fix-129-bubble-model-menu` pour la nuit |
| **1 — Bulle et clic** | O1 (règles 1 à 4 de la variante C) et O2 revérifiés par le test général, O3, O5 | 1 à 1,5 jour + 1 nuit | Lot 0 ; règles 1 à 4 fusionnées depuis `fix-129-bubble-model-menu` |
| **2 — Premier lancement et autorisations** | O8, O10 | 1 jour + 1 nuit | Lot 0 |
| **3 — Projets locaux seulement** | Pas l'extinction elle-même (Builder, `a65f50b`). Le parcours `local_en_ligne` (adresse du réseau local = éteint ; `file://` et fenêtre privée selon la règle), la réécriture de `chat_qa_run.py:600` et `chat_qa_v12.py:220`, le retrait des tests des chemins « site en ligne », O12 | 0,5 à 1 jour + 1 nuit | `VALXXII/125-annotation-onboarding` fusionnée dans main |
| **4 — Défilement et vitesse** | O4, **règle 5 de #130** (recalcul pendant le défilement, glissement de 120 ms), O6 | 1,5 à 2,5 jours + 1 nuit. Incertain : O4 peut finir en décision de design | Lot 1 |
| **5 — Agents réels, nettoyage** | O9 : un essai réel Claude Code et un essai réel Codex sur une copie de test. O7 : retrait complet du lot. O11 ; passage avec le Narrateur | 1,5 jour + 1 nuit | — |

Total estimé : 6,5 à 8 jours de travail et 6 nuits. Règle de sortie : un lot ne part que si le test général est vert **en
entier** sur le commit exact qui sort.

## 4. Test général

Tout existe déjà par morceaux (`tests/chat/final/run_final.py`, `tests/chat/run_suites.py`). Ce qui manque : une seule
entrée, la liste complète, et l'interdiction des raccourcis. À écrire au lot 0, dans `tests/annotation-general/`.

### 4.1 Règles « pas de raccourci »

1. **Une seule liste** (`manifest.json`) de toutes les suites, avec leur commande. Le lanceur exécute tout, dans l'ordre.
2. **Vert = tout a tourné et tout a réussi.** Une suite absente, sautée, en dépassement de temps ou filtrée = rouge.
   `--only` existe pour chercher un bug, mais écrit « partiel, non valable » dans le rapport.
3. **Rien n'est relancé.** Un échec « qui passe la deuxième fois » reste un échec.
4. **Le rapport porte le commit, la version et l'état de l'arbre.** La sortie n'est permise que si ce commit est celui
   qui part.
5. **Table de couverture** (`coverage.json`) : chaque ligne de l'inventaire (C1 … F11, R1 … R48, O1 … O12, #129, #130)
   pointe vers au moins un test. Une ligne sans test, ou dont le test n'a pas tourné, = rouge.
6. **Les seuils sont dans la liste**, chacun avec la décision qui le fixe. Deux sortes. **Bloquant** : clic = bulle en
   3 s, bulle entière, boutons atteignables. **Objectif** (décision 5) : ouverture < 150 ms, suivi ± 2 px ; mesurés et
   écrits dans le rapport à chaque passage, ils ne font pas échouer le test. Changer un seuil ou sa sorte est une
   décision du propriétaire.
7. **Les attentes modifiées sont affichées** : le rapport liste les fichiers de test changés depuis le dernier vert.
8. **Sécurité** : copie du build et profil temporaire sous `build/`, jamais `%LOCALAPPDATA%\browser` ; pages locales
   sur 127.0.0.1 ; faux agent ; `BROWSER_ANNOTATIONS_TEST=1` ; configurations Claude / Codex redirigées et empreintes
   vérifiées avant / après ; tout l'arbre de processus dans un Job Object `KILL_ON_JOB_CLOSE` ; une seule suite en vraie
   fenêtre à la fois ; coffre de mots de passe, SmartScreen et bac à sable laissés tels quels.

### 4.2 Partie sans fenêtre (journée)

```
python tests/annotation-general/run.py --day
```

Durée mesurée le 24/11 : **13 min 10 s** (11 suites). Aucune fenêtre à l'écran.

| Suite (id du manifeste) | Commande | Couvre |
|---|---|---|
| `i18n` | `python tools/i18n/check_strings.py` | Textes |
| `chat-units` | `python -I tests/chat/run_tests.py` | Placement de la bulle (#130 C), sites locaux, dossier, consigne, magasin ; contrats de l'hôte |
| `model-switch-128` | `python -I tests/chat/model_switch_128_test.py` | Changement de modèle et de fournisseur (#128) |
| `annotation-units` | `python -I tests/annotation-general/units.py` | Où Annoter est allumé (#125), notes d'une fenêtre privée, tutoriel dessiné hors écran. Avant le lot 0, seul le lanceur de l'installateur les compilait |
| `chat-panel` | `python -I tests/chat-panel/run.py` | Page du chat : états Paper, clavier, accessibilité, menus, autorisations, brouillons |
| `page-v4` | `python tests/annotations-ui/page_v4.py` | Script de la page : cadres, étiquettes, inspecteur, Ctrl maintenu. N'était dans aucun ancien lanceur |
| `fake-agent` | `python -I tests/chat/final/fake_agent_scenarios.py` | Le faux agent se comporte comme un vrai (annonces, autorisation, erreurs) |
| `snapshots` | `python -I tests/snapshots/run_tests.py` | « Tout annuler », compteur de fichiers |
| `annotations` | `python -I tests/annotations/run_tests.py` | Magasin et pont MCP : annotation gardée, lue, prise, terminée ; connexion Claude / Codex sur dossiers redirigés |
| `annotations-ui` | `python tests/annotations-ui/run.py --no-window` | Bandeau, lanceur, historique, note. La nuit : avec ses fenêtres |
| `acp` | `python -I tests/acp/run_tests.py --quick --no-console` | Client ACP, runtime des agents. La nuit : avec la connexion en console et les vrais téléchargements |

### 4.3 Partie en vraie fenêtre (nuit)

```
python tests/annotation-general/run.py --night --build
```

À lancer depuis un dossier de travail préparé (`setup.ps1`), sur le commit exact à valider, arbre propre. Session Windows
ouverte, écran non verrouillé (le lanceur refuse de démarrer sinon), personne au clavier, aucune autre suite en vraie
fenêtre en même temps.

Durée estimée : **1 h 45, à prévoir 2 h 15**. Base : build neuf ≈ 15 min ; la partie sans fenêtre rejouée en entier
≈ 17 min (13 min mesurées, plus les téléchargements et la console du test ACP) ; suites du dépôt ≈ 13 min ; 12 suites du
chat en vraie fenêtre ≈ 14 min ; parcours ≈ 21 min ; monkey 3 graines ≈ 25 min (durées du 22/11).

Ordre (celui de `manifest.json`, 32 suites) :

1. Build neuf du commit exact dans `build/annotation-general-app` (`tools/release/build.py … --channel dev --preview`).
   Si le build n'est pas celui du commit testé, les suites en vraie fenêtre ne sont pas lancées et le passage est rouge.
2. Les 11 suites de la partie sans fenêtre, dans leur version complète.
3. `smoke` (`test.ps1`), `shortcuts`, `annotation-settings`, `accessibility`, `uia-offscreen`.
4. Chat en vraie fenêtre : `chat-v1`, `v12`, `comment`, `comment-switch`, `comment-v2`, `v3`, `v4`, `v4b`, `v5`, `v5b`,
   `r43`, `comment-129-130`.
5. `journeys` : les 39 parcours (R1 à R48 et la revue).
6. `monkey-1`, `monkey-2`, `monkey-3` : 320 actions chacun, invariants après chaque action.
7. Rapport : `build/annotation-general/night/report.md` ; pour chaque échec, la suite, son journal, ses lignes en échec.

Les nouveaux parcours des lots 1 à 5 (O3, O5, O8, O10, `local_en_ligne`, règle 5) s'ajoutent à ces suites ; tant qu'ils
ne sont pas écrits, `coverage.json` garde un trou pour chacun, avec son lot.

Ne sont **pas** dans ce test : `tests/update-integration/run_all.py` (installateur, fenêtres réelles, tournée de nuit à
part), `test-password-vault.ps1`, `tests/landing/`.

### 4.4 Comportement attendu de la bulle (décision 1 du 24/11)

**#130 l'emporte sur R29.** La règle du 20/11 (« position figée, jamais replacée pour tenir dans la fenêtre,
l'utilisateur défile ») n'est plus vérifiée. Le test général vérifie à la place :

1. **Bulle entière.** Tant qu'une bulle est affichée, toute sa surface est dans la zone de page de la fenêtre : jamais
   coupée par un bord, jamais sur les barres du navigateur. Vérifié à l'ouverture, après chaque changement de hauteur
   (nouvelle carte, autorisation, erreur, diff ouvert ou fermé, poignée), après un redimensionnement de la fenêtre et
   après un changement de zoom.
2. **Actions toujours atteignables.** Quand une carte attend une réponse, Refuser et Autoriser sont visibles en entier
   et répondent à un vrai clic. La saisie et son bouton d'envoi aussi. Y compris quand la page du site ne peut pas défiler.
3. **Rien de perdu.** Si le contenu est plus haut que la place, il défile à l'intérieur de la bulle ; aucun message
   n'est rogné sans moyen de le lire.
4. **Pas de saut.** Ce qui était déjà vrai reste vérifié : pas d'image intermédiaire, les cartes déjà affichées ne
   bougent pas quand une nouvelle arrive, un menu ne déplace pas la surface.

**Variante C, choisie le 24/11** (ticket #130 ; planche « #130 Bulle sans place dessous · C remonte le long du côté
(validée) », page Paper « MCP - Design UI UX », lue le 24/11). Ses cinq règles, telles qu'écrites sur la planche,
deviennent des vérifications :

| Règle | Vérification | Sort |
|---|---|---|
| 1. Côté : 12 px entre l'élément (cadre de sélection) et la bulle, « comme dans l'app aujourd'hui » (planche corrigée par Designer le 24/11 ; elle disait 8 px) | Rien ne change : la bulle est à 12 px de l'élément (`BubbleGapCss`, `src/chat-comment.h:25`). Les contrôles de distance actuels restent. Les 8 px ne valent que pour les marges aux bords de la page affichée (règles 2, 3, 9) | Déjà en place |
| 2. Plus de place dessous : la bulle remonte jusqu'à ce que son bas soit à 8 px du bas de la page affichée ; elle reste à côté de l'élément, sans le couvrir | Bas de la bulle ≤ bas de la zone − 8 px ; aucun pixel commun avec l'élément | Branche en cours |
| 3. Jamais plus haut que 8 px sous le bandeau du mode Annotation | Haut de la bulle ≥ bas du bandeau + 8 px | Branche en cours |
| 4. Même en remontant elle ne tient pas : hauteur = page affichée − 16 px, le début du fil défile dans la bulle avec le fondu du haut, le champ de saisie et le pied restent toujours visibles | Hauteur mesurée ; la saisie et le pied sont dans la zone et répondent au clic. Pour Refuser / Autoriser : question 3 du §5 | Branche en cours |
| 5. Si la page défile ou si la fenêtre change de taille, la position est recalculée, sans saut (glissement de 120 ms au plus) | Parcours `o1_regle5_defilement` (§3.2), plus le même contrôle pendant un redimensionnement | **Lot 4** |

Conséquences pour la lecture des règles du test :

- **Avant le lot 4**, « bulle entière » est vérifié à l'ouverture, à chaque changement de hauteur et après un
  redimensionnement, pas pendant un défilement. **À partir du lot 4**, aussi pendant et après un défilement, une fois le
  glissement fini.
- Le seuil de suivi ± 2 px (O4) ne vaut que lorsque la bulle est à sa place normale à côté de l'élément. Quand elle est
  retenue par un bord, elle ne suit plus l'élément au pixel : on mesure alors la durée du glissement et l'absence de saut.

- La bonne place après un redimensionnement (sans animation) est déjà dans le lot 1 (O5). Le lot 4 y ajoute le
  glissement de la règle 5.

Les six cas que la première planche ne montrait pas sont **validés par le propriétaire le 24/11** (planche « #130 Bulle
sans place dessous · C règles (validées) », à droite de la planche C, règles 6 à 11) :

| Règle | Vérification | Sort |
|---|---|---|
| 6. L'élément sort de l'écran au défilement : la bulle le suit et sort avec lui, elle revient avec lui | « Bulle entière » ne vaut que tant que l'élément est à l'écran. Élément sorti : aucune bulle retenue au bord, aucune bulle fermée ; au retour, même conversation, même brouillon | Lot 4 (avec la règle 5) |
| 7. Le navigateur ne fait jamais défiler la page pour montrer une carte (ancien R47 retiré) | Pendant un tour avec autorisation, la position de défilement de la page du site ne change pas sans action de l'utilisateur | Lot 1 |
| 8. Carte d'autorisation : Refuser / Autoriser restent dans le pied toujours visible | Les deux boutons sont dans la zone de page et répondent à un vrai clic, quelle que soit la longueur du fil | Lot 1 |
| 9. Élément sans côté libre : dessous, sinon dessus, à 12 px ; sinon dans l'élément, coin bas-droit, à 8 px du bord | `section.hero` et `div.plans` : position mesurée pour chacun des trois cas (12 px dessous ou dessus, 8 px dans l'élément). Remplace R44 (coin le plus proche du clic, 12 px) | Lot 1 |
| 10. Jamais par-dessus l'élément : la bulle bascule de l'autre côté (dessus ↔ dessous), sinon hauteur limitée + défilement (règle 4) | Aucun pixel commun avec l'élément tant qu'un côté a la place ; sinon règle 4 | Lot 1 |
| 11. « Réduire les animations » : placement direct. Défilement continu : la bulle colle à l'élément, elle ne glisse qu'au changement de côté | Mouvement réduit : aucune image intermédiaire. Défilement continu : le glissement de 120 ms n'apparaît qu'au changement de côté | Lot 4 |

« Lot 1 » veut dire : vérifié par le test général dès que la branche `VALXXII/fix-129-bubble-model-menu` est fusionnée ;
si un de ces points n'y est pas encore codé, il est corrigé au lot 1.

Tests écrits pour R29 dont l'attente change. Chaque changement sera listé dans le rapport (règle 7 du §4.1) avec la
référence « décision 1 du 24/11 » :

| Test | Attente actuelle (R29) | Devient |
|---|---|---|
| `tests/chat/chat_units_test.cpp:579` | « never moved to fit the window (the user scrolls) » : le bas peut dépasser la zone | **Fait** (branche du correctif) : « #130 wins over R29 », la bulle glisse vers le haut |
| `tests/chat/chat_units_test.cpp:587` | Une seule exception, en fin de document, un seul décalage | **Gardé** : la fonction de fin de document existe encore dans le produit ; son test reste vrai |
| `tests/chat/host_contract_test.py:286` | La hauteur n'est jamais limitée par la place sous le bord fixe | **Fait** : hauteur limitée à la page affichée − 16 px (règle 4) |
| `tests/chat/final/journeys.py:128` (`r44_…`) | Hauteur naturelle, jamais réduite pour tenir ; dans l'élément, au coin le plus proche du clic | **Fait** pour la hauteur (naturelle, ou page − 16 px) et « bulle entière ». La règle 9 (dessous, dessus, coin bas-droit à 8 px) attend son code : lot 1 |
| `tests/chat/final/journeys.py:150` (`r47_…`) | La page du site défile pour montrer la carte | **Écrit, éteint** (`C130_LOT1_RULES`) : la page ne défile jamais toute seule (règle 7). Le défilement automatique est encore dans le produit : lot 1 |
| `tests/chat/final/journeys.py:383` | Pendant le tour, la bulle ne bouge jamais | **Fait** : même côté (bord gauche fixe) et entière dans la zone |
| `tests/chat/chat_qa_comment_v4.py:173` | La surface garde son coin au pixel pendant le tour et le diff | **Fait** : coin gardé, sauf glissement vers le haut annoncé par le journal |
| `tests/chat/chat_qa_comment_v4b.py:280` (T4) | Fin de document : un décalage au plus | **Fait** : entière, et ne fait que monter |

Restent tels quels : « jamais sur les barres du navigateur » (`chat_units_test.cpp:589`), l'arrivée douce d'une nouvelle
carte (`tests/chat-panel/run.py:875-879`), la poignée et ses bornes (R40).

La branche `VALXXII/fix-129-bubble-model-menu` modifie déjà une partie de ces fichiers : les numéros de ligne sont ceux de
main `86697e0` et seront à relire après sa fusion.

## 5. Décisions et maquettes

| # | Décision | Pourquoi |
|---|---|---|
| 1 | ~~#130 remplace-t-il R29 ?~~ **Tranchée le 24/11 : oui.** La bulle reste toujours entièrement dans la fenêtre. **Variante C choisie** : elle remonte le long du côté de l'élément | Attentes du test au §4.4 ; règles 1 à 4 et 7 à 10 sur la branche en cours puis au lot 1, règles 5, 6 et 11 (défilement) au lot 4 |
| 2 | ~~Fichiers locaux (`file://`) et fenêtre privée après #125 ?~~ **Tranchée le 24/11 : Annoter reste allumé** sur les deux | Comportement d'aujourd'hui gardé (note native, pas de chat) ; à tester au lot 3 |
| 3 | ~~Le « lot » (Traiter les annotations ensemble) ?~~ **Tranchée le 24/11 : retiré en entier**, hôte, page et tests | Lot 5 |
| 4 | ~~Un essai réel avec Claude Code et un avec Codex ?~~ **Tranchée le 24/11 : oui**, une petite demande chacun, sur une copie de test seulement | Lot 5 ; comptes du propriétaire, rien d'autre que la copie |
| 5 | ~~150 ms et ± 2 px : obligatoires ou objectifs ?~~ **Tranchée le 24/11 : objectifs.** Rapportés, jamais bloquants | Marqués « objectif » dans `tests/annotation-general/manifest.json` |
| 6 | ~~Qui code l'extinction hors projet local ?~~ **Tranchée le 24/11 : Builder**, avec l'onboarding #125 | Même fichier (`src/annotation-mode.h`). Notre lot 3 ne garde que les tests et le reste de O12 |

Maquettes : je n'ai **pas ouvert Paper** pour ce document. Points à confirmer avec Designer avant de coder :

- O1 : planches validées « #130 Bulle sans place dessous · C remonte le long du côté (validée) » et « … · C règles
  (validées) », lues le 24/11 : 11 règles, plus aucune question ouverte.
- #125 : bouton Annoter éteint et son info-bulle (planche A2 de la v1 ; la v2 attend le choix du propriétaire).
- O4 : si le suivi parfait est impossible, une alternative est à dessiner (R39b le prévoit : « proposer sans appliquer »).
- C3 : bulle gardée à sa dernière place quand son élément a disparu pendant le travail : aucun état visuel connu.

### Questions pour Designer — #130, variante C : toutes réglées

Les 6 questions posées le 24/11 ont leur réponse sur la planche « C règles (validées) », règles 6 à 11 (§4.4) :

| Question | Réponse validée |
|---|---|
| 1. Élément sorti de l'écran | La bulle le suit et sort avec lui, elle revient avec lui (règle 6) |
| 2. Défilement automatique de la page du site | Retiré : le navigateur ne déplace jamais la page de l'utilisateur (règle 7) |
| 3. Carte d'autorisation | Refuser / Autoriser sont dans le pied toujours visible (règle 8) |
| 4. Élément sans côté libre | Dessous, sinon dessus, à 12 px ; sinon dans l'élément, coin bas-droit, à 8 px du bord (règle 9) |
| 5. Bulle sous ou sur l'élément | Jamais par-dessus : elle bascule de l'autre côté, sinon hauteur limitée et défilement intérieur (règle 10) |
| 6. Mouvement | Placement direct avec « Réduire les animations » ; en défilement continu elle colle à l'élément et ne glisse qu'au changement de côté (règle 11) |

## 6. Suites passées le 24/11 à 2 h, avant le lot 0 (sans fenêtre, main `86697e0`)

| Suite | Résultat |
|---|---|
| Barrière de traduction | 0 constat |
| Unités du chat | 486 vérifications, 0 échec ; contrats 110 / 110 |
| Page du chat (sans tête) | 802 / 802 |
| Instantanés (`--quick`) | Tout passe |
| Magasin et pont MCP (`--quick`) | Tout passe (199 s) ; configurations réelles de Claude / Codex inchangées |
| Client ACP | **Arrêté avant de tourner** : il ouvre une console visible, interdit hors tournée de nuit |
| Tout ce qui ouvre une fenêtre | Non lancé |

Ces résultats disent que le code de main est cohérent avec ses tests sans fenêtre. Ils ne disent rien des 12 sujets
ouverts : aucun n'est visible sans vraie fenêtre.

## 7. Lot 0 — ce qui est fait (24/11)

Aucun comportement du produit n'a changé. Une seule ligne du produit est touchée : un message de journal
(`src/chat-host-ui.h`, diagnostic `c419d92`).

| Élément | Où |
|---|---|
| Documents de retours commités (R1–R48, V2, plan final, QA du 22/11, deux briefs) | `design/annotation-feedback/` |
| Deux commits de diagnostic de `VALXXII/annotations-chat` fusionnés | `c419d92` (raison d'un clic refusé), `49af0f5` (test R7) |
| Test général | `tests/annotation-general/` : `run.py` (`--day`, `--night --build`, `--list`, `--check`, `--only`), `manifest.json` (32 suites), `coverage.json` (101 lignes), `units.py`, `README.md` |
| Job Object | Chaque suite dans le sien (`KILL_ON_JOB_CLOSE`), créée suspendue ; un processus resté ouvert = échec |
| Test ACP sans console | `tests/acp/run_tests.py --no-console` saute la seule connexion en console visible ; la nuit la rejoue |
| 150 ms et ± 2 px en objectifs | `chat_qa.objective()` dans `qa_final.py`, `monkey.py`, `chat_qa_comment_v3.py`, `v4b.py`, `v5.py` |
| Tests de R29 alignés sur #130 C | Tableau du §4.4 ; règles pas encore codées : assertions écrites, éteintes par `C130_LOT1_RULES` et `C130_SCROLL_RULES` (`tests/chat/final/qa_final.py`) |
| main fusionné | jusqu'à `b3480ba` (PR #133 et #134) |

### Premier passage de la partie sans fenêtre (24/11, 7 h 40, commit `3d95bb8`, arbre propre)

**Verdict : ROUGE.** 10 suites sur 11 réussies, 13 min 10 s. Aucune relance, comme le veut la règle 3.

| Suite | Résultat | Durée |
|---|---|---|
| `i18n` | réussi | 3 s |
| `chat-units` | réussi | 22 s |
| `model-switch-128` | réussi | < 1 s |
| `annotation-units` | réussi | 37 s |
| `chat-panel` | **échec : 1 vérification sur 873** | 215 s |
| `page-v4` | réussi | 45 s |
| `fake-agent` | réussi | 31 s |
| `snapshots` | réussi | 56 s |
| `annotations` | réussi | 270 s |
| `annotations-ui` (hors écran) | réussi | 35 s |
| `acp` (sans console, sans téléchargement) | réussi | 76 s |

La vérification en échec : « keyboard: Entrée = Autoriser (toujours, case cochée) » (`tests/chat-panel/run.py:1212`).
Les fichiers de `assets/chat-panel/` et de `tests/chat-panel/` sont identiques à main : l'échec n'est pas dû à cette branche.

Cause très probable, lue dans le code et pas encore prouvée par un essai : **le test dépend de la vitesse de la machine.**
Depuis le correctif de sécurité du 23/11 (`c9e91cf`), la carte d'autorisation n'accepte Entrée que 600 ms après son
apparition. Le test met l'heure d'apparition à 0, ce qui ne suffit que si la page est chargée depuis plus de 600 ms. Sur
une machine occupée c'est le cas (802 / 802 à 2 h du matin) ; sur une machine libre le test arrive avant et rien n'est
envoyé. Ce n'est donc pas un défaut du produit, mais c'est un test qui passe ou échoue selon la charge : exactement le
genre de preuve fragile que ce plan veut retirer. Correctif proposé, une ligne du test (`__shownAt = -1e6` au lieu de `0`),
**non appliqué** : `tests/chat-panel/` est réservé pendant le travail de Builder sur #128. Lié à O10.

Conséquence : 16 lignes de l'inventaire n'ont pas de preuve dans ce passage (celles que seule cette suite couvre : M2, F1,
F2, F10, R3, R4, R5, R7, R8, R11, R12, R13, R17, R21, R23, R30). 48 lignes sont prouvées, 27 ne se vérifient que la nuit.

Sécurité du passage : configurations réelles de Claude et de Codex inchangées, aucun processus resté ouvert, aucune fenêtre.

### Ce que le lot 0 n'a pas pu vérifier

- Les scripts en vraie fenêtre modifiés (`qa_final.py`, `journeys.py`, `monkey.py`, `chat_qa_comment_v3.py`, `v4.py`,
  `v4b.py`, `v5.py`) n'ont été que relus et analysés pour leur syntaxe. Leur premier vrai passage est la prochaine nuit.
- Le lanceur n'a jamais tourné en mode `--night`.
- Le garde-fou « processus resté ouvert = échec » peut se montrer trop strict avec les vraies fenêtres ; s'il l'est, la
  nuit le dira avec le nom du processus.
