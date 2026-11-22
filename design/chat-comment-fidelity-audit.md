# Mode Commentaire v2 — audit de fidélité à Paper

Audit du 19 novembre 2026 (worker F). Référence : `design/chat-comment-paper-values.md` (valeurs lues dans Paper, commits c4e6a46 et 0c9d129) et les captures Paper de `build/chat-qa/comment-v2/paper/`. Décisions admises : `chat-comment-v2-retours.md` (R1 à R12), réponses du coordinateur (4 modes de `14RK-0`, placement +12 / −4, vraie taille dans l'étiquette), brief règle 7 (« Demander » au lieu de « Ask »).

Ce qui a été examiné :
- **Vraie fenêtre, côté hôte (H)** : `build/chat-qa/comment-v2/r*.png` et `host-report.json`. Mesures au pixel sur les images (échelle 125 % : 1 px CSS = 1,25 px écran).
- **Vraie fenêtre, côté page (P)** : `panel/*-fenetre.png`, `panel/c-menu-*.png`.
- **Valeurs DOM (P)** : `panel/ecarts.md`, `tests/chat-panel/fidelity.json`, soit 2 076 valeurs comparées nœud par nœud au relevé Paper, avec 28 écarts déclarés justifiés. J'ai contrôlé chaque justification ci-dessous.

Aucune vraie fenêtre n'a été lancée de mon côté.

## Cinquième passe — 22 novembre 2026 (état actuel, build 0.3.561, HEAD 1d72bb0)

### Sources

- **Parcours de Q** : `build/chat-qa/final/journeys.json`, 39 parcours rejoués entre 16 h 42 et 16 h 59, après le dernier changement de l'hôte (e82d523, 16 h 35). 38 sont entièrement verts, 1 a une étape en échec.
- **Tests aléatoires (« monkey ») de Q** : `final/monkey-1` à `3`, de 16 h 59 à 17 h 18, sur le même code. Ils s'arrêtent à 30 échecs ou finissent avec 25 à 32 échecs.
- **Captures `comment-v5`** : R39, R39b, R41, E4 et R40 (14 h 15 à 16 h 53). Le `report.json` de comment-v5 (15 h 47) **date d'avant** la correction R39 f3842bb (15 h 48) : pour R39, ce sont les tests aléatoires qui font foi.
- **Décisions** R1 à R48 et arbitrages de placement : cotes de Paper +12 / −4, glissement le long d'un côté, par-dessus l'élément en dernier recours, bulle entière à l'ouverture (e26c39c).
- **Mes contrôles visuels**, sans vraie fenêtre.

Les captures montrent par endroits le widget d'une autre application (heure, bouton pause) en haut de l'écran. Il ne vient pas du navigateur.

### Conforme

| Décisions | Preuve |
|---|---|
| R1, R4, R13, R20, R21, R28, R30 (forme de la bulle, modes, saisie) | Parcours `r20_r30_r01_bulle_forme` vert : 320 px, coins 16, mode en icône, modèle · effort dès la première image, entrée 250 ms |
| R2, R27, R32 (marque seulement après envoi, un seul contour) | `r02_r32_marque_apres_envoi` vert ; `m6_clic_sans_envoi_puis_panneau` vert |
| R6, R18 (rouvrir, A → B → A → B), E1, E2, C1 à C3 | `r06…`, `abab`, `e1_iframe_composant_web`, `e2…`, `c1…`, `c2…`, `c3…` verts. Les défauts 1 et 6 de `defauts-passe1.md` sont corrigés |
| R7, R15, R19, R22 | `r07_r15_r19_r22_details` vert. `e4_defilement_puis_menu` : le bord haut de la surface ne bouge pas pendant l'ouverture du menu (81 images, haut constant à 41) : défaut 5 de la passe 1 corrigé |
| R9, R47 (autorisation entière, pas grisée) | `r47_autorisation_defilement` : carte entière, Refuser et Autoriser à l'intérieur de la bulle, opacité 1, aussi après défilement |
| R11, R23, R24 (panneau) | `r11_r23_r24_panneau` vert |
| R14, R33, R45 (étiquettes identiques au survol, pendant la capture et sélectionnées) | `r14_r45_etiquettes` : 0 % de pixels différents sur 171 à 177 images, pour 3 éléments, à 100, 125 et 150 %. Défaut 3 de la passe 1 corrigé |
| R26, R31, R34, R35, R42 | Parcours verts (`r26…`, `r31_ctrl_maintenu`, `r34_r35…`, `r42_modele`). Défaut 2 de la passe 1 (R31) corrigé |
| R36, R38, R40 (diff en place, même taille, poignée) | `r38_r40_diff_poignee` vert. `comment-v5/r40-*` : le diff s'ouvre à la même taille que la bulle, l'agrandissement garde le coin haut gauche fixe. Poignée visible en bas à droite (`diff-agrandi.png`) |
| R41 (un menu se transforme en l'autre) | `r41-modele-vers-mode-09` à `14` : la surface reste opaque, se déplace par petits pas (≈ 192 → 200 → 208 → 225 px) avec fondu croisé du contenu ; ni sortie puis entrée, ni éclair autour |
| R43, R48, R12 (lignes de suivi, langue, pas de curseur bleu) | `r43_r48_annonces_markdown` vert |
| R44 (entière à l'ouverture, hauteur naturelle jusqu'à 520 px) | `r44_grand_element_taille_utilisateur` vert (18 étapes) : section.hero et div.plans entières, côté choisi écrit dans le journal ; avec un long fil, la surface fait 650 px écran = 520 CSS et le fil défile à l'intérieur |
| R46 (mode stable en collant) | `r46_coller_mode_stable` : 174 relevés, toujours « Demander ». Voir quand même F3 |
| Placement standard | Dans les parcours, les distances de Paper (+12 / −4) sont vérifiées par l'invariant de Q et passent. Mes relevés sur `diff-agrandi.png` sont cohérents à ±1 px CSS près |

### Défauts restants

| # | Décision | Constat (vraie fenêtre, Q) | Correctif exact | Propriétaire |
|---|---|---|---|---|
| F1 | Placement Paper +12 / −4 et R44 | **Après une bascule Panneau → Commentaire suivie d'un clic, la bulle est placée par rapport à un autre rectangle que celui de l'élément cliqué.** Le journal de l'hôte annonce `element 1430x603`, `650x55`, `427x67` quand l'élément sous le clic mesure 577×441, 591×426 et 492×300. Résultat : la bulle est à 68 px sous l'élément et décalée de 619 px à gauche (monkey-1 #101), par-dessus l'élément à −508 px (monkey-2 #224), à −13,6 px (monkey-3 #117), ou au-dessus avec −52,8 px (monkey-3 #125). e82d523 (« a click's rectangle measured in another viewport ») ne suffit pas : ces échecs sont postérieurs | Après une bascule de disposition, ne placer la bulle qu'avec le rectangle de l'élément mesuré **après** le changement de zone de page, dans le même repère que la page, et pour l'élément réellement sélectionné. Ajouter au journal l'élément et le rectangle utilisés, et un test « bascule puis clic » qui compare la distance à Paper | H |
| F2 | R39 (bulle collée à l'élément au défilement) | Tests aléatoires, après f3842bb : l'écart bulle–élément varie de 18,4 à 98,4 px pendant un défilement (monkey-3 #5, #9, #51, #78, #96, #169), jusqu'à **320,8 px** (monkey-2 #87). Objectif : ±2 px | Trouver le cas qui reste (défilement pendant une ouverture ou juste après une bascule, molette rapide) avec la trace de suivi, puis le corriger. Rejouer `chat_qa_comment_v5.py follow()` sur le build actuel | H |
| F3 | R46 / R28 (le mode ne change jamais sans l'utilisateur) | monkey-1 #81 à #101 : après « Commentaire puis clic 50 ms plus tard », la saisie affiche « Demander » alors que l'utilisateur avait choisi « Auto », et cela reste faux sur une vingtaine d'actions, panneau compris | Pendant une bascule et l'ouverture qui suit, garder et renvoyer à la page le mode **choisi par l'utilisateur**, jamais celui de démarrage de la session ni la valeur par défaut. La page n'écrase pas un choix de l'utilisateur avec un état plus ancien | H (P si la page écrase la valeur reçue) |
| F4 | Bulle fermée = rien à l'écran | monkey-2 #102 à #106 et #261 à #265 : après un clic dans `#qa-card` (contenu imbriqué) ou un retour depuis une autre application, la bulle est fermée mais **une région de 400×435 et de 400×105 px reste à l'écran** pendant plusieurs actions | À la fermeture, vider la région de la fenêtre de la bulle (ou la cacher) dans tous les chemins, y compris le clic dans un cadre imbriqué et le retour au premier plan | H |
| F5 | R18 (ouverture < 150 ms) | `m5_bascule_puis_clic` 3e essai : 152 ms. Tests aléatoires, « bascule puis clic » : 166 à 235 ms, et un clic sur div.plans à **597 ms** (monkey-2 #189). Les ouvertures sans bascule sont à 25 à 90 ms | Réduire le temps entre une bascule de disposition et le premier clic (page de la bulle déjà prête après la bascule) ; repérer la cause des 597 ms dans le journal d'ouverture | H |
| F6 | Message affiché là où il a été envoyé | monkey-2 #29, monkey-3 #63 et #70 : après un envoi dans la bulle, le message n'est pas montré dans la conversation de cette bulle | Retrouver la conversation de l'envoi dans le journal et la page ; le message doit apparaître tout de suite dans la bulle d'où il part | H (P si la page le reçoit sans l'afficher) |
| F7 | Accessibilité de la fenêtre | monkey-3 #214 : après une molette, l'arbre UI Automation de la fenêtre principale passe de 113 à 22 éléments | Retrouver ce qui coupe l'arbre (fenêtre de la bulle ou région ajoutée devant la page) et le rétablir | H |
| F8 | R22 | `+` garde `aria-haspopup="menu"` alors qu'il ouvre directement le choix d'image (défaut 8 de la passe 1, pas de correctif trouvé) | Retirer `aria-haspopup` et `aria-expanded` du « + » quand il n'ouvre pas de menu | P |

### À trancher ou à vérifier (pas comptés comme défauts)

- **monkey-3 #129** (« open → window visible » : région de 1 px quand aucun élément n'est visible) : selon R39b, la bulle défile comme le contenu de la page et sort avec son élément. Qu'elle ne soit plus visible est donc correct. **C'est l'invariant de Q qui est à revoir.**
- **Hauteur « naturelle » quand la zone de page fait moins de 520 px** (monkey-2 #108, #199, #202 ; monkey-3 #227, #232, surface de 497 CSS pour une zone de 522) : R44 ne peut pas être tenu au-delà de la zone visible. Le fil défile alors dans la bulle, ce qui me paraît le bon comportement. À confirmer par l'utilisateur.
- **`section.hero` (bulle à x = 2 px écran)** : la bulle est sous l'élément, alignée à −4 puis ramenée à 2 px du bord de la fenêtre (comme les étiquettes, R15). C'est cohérent avec les arbitrages.
- **R39b au bord haut** : un contact avec la barre du navigateur était mesuré à 15 h 47, avant f3842bb. Pas de nouvelle mesure image par image : à rejouer avec `edge_frames()`.

## Quatrième passe — 20 novembre 2026, soir (R26 à R38, commits a26587f, fee8927, 53fe3ac, 9b15972, 6b5f65d ; v3 rejouée en 825f9f2)

Sources :
- **Vraie fenêtre H** : `build/chat-qa/comment-v4/` (`report.json` 14/14 ; `r26`, `r32`, `r34`, `r38-diff-ouvert`, `r37-defilement-00` à `12`) et `comment-v3/` rejoué à 16 h 45 (`report.json`, 18 contrôles dont 1 en échec).
- **Sans fenêtre H2** : `tests/annotations-ui/page_v4.py`, **relancé par moi**, 52/52 OK (22 contrôles unitaires + 30 dans Chromium sans écran à 100, 125 et 150 %).
- Mes mesures au pixel sur les captures.

Remarque : un petit widget d'une autre application (heure « 1:36:45 », avatars) recouvre le coin haut droit des captures v4. Il ne vient pas du navigateur.

### R26 à R38

| # | Décision | Preuve | Verdict |
|---|---|---|---|
| R26 | Survol d'une puce : défilement doux vers l'élément, montré comme au survol, retour sans redéfiler | `r26-survol-puce.png` : puce « span.field-label » survolée dans le panneau, l'élément est montré avec son cadre et ses deux étiquettes Inter (« 397x18 »). Sans fenêtre : 48 positions de défilement de 0 à 2 290 (pas de saut), inspecteur seulement s'il y a padding ou gap, retour sans redéfiler, pas de défilement si l'élément est déjà visible, cadre de la bulle conservé | Conforme |
| R27 | Un seul contour : même rectangle pour le cadre conversation et le cadre survol / sélection | Sans fenêtre, à 100, 125 et 150 % : les deux cadres couvrent les mêmes pixels (1 750, 2 192 et 3 234 px, 0 différent), aucun double bord quand ils sont ensemble, bordure de 2 px pour les deux | Conforme (pas de capture en vraie fenêtre de ce cas) |
| R28 | Bas de la saisie juste et stable dès la première image | `report` : « Sonnet 5.5 Moyen » dès la première image, hauteur 84 px constante. `r38` : effort et chevron présents | Conforme dans la bulle. Voir **O1** pour le panneau |
| R29 | Position figée une fois posée, croissance dans un seul sens | `report` : pendant les nouvelles cartes, la modification qui déplace l'élément et le diff, la surface garde son décalage à l'élément (+315 ; −4 px écran, soit +12 / −4 CSS) | Conforme. L'exception près du bas de la page n'est pas testée (**T4**) |
| R30 | Entrée en 250 ms, glissé de 12 px, `cubic-bezier(.23,1,.32,1)`, fondu seul en mouvement réduit | `panel.css` l. 639 et 646 : `b-in 250ms var(--ease-out)`, avec `--ease-out` = `cubic-bezier(0.23, 1, 0.32, 1)`, glissé de 12 px ; fondu de 120 ms en mouvement réduit | Conforme (vérifié dans le code) |
| R31 | Ctrl maintenu : le site comme d'habitude | Sans fenêtre : curseur de la page pendant Ctrl, pas de cadre de survol, le clic va au site, rien n'est sélectionné, Ctrl+C reste à la page ; au relâchement, le viseur et le cadre reviennent tout de suite | Conforme côté page. Le curseur réel et le Chat à droite en vraie fenêtre ne sont pas capturés (**T5**) |
| R32 | Icône et bordure « conversation » seulement après un envoi, dans les deux modes | `r32-rien-apres-echap.png` + `report` : rien sur l'élément après clic puis Échap, en Commentaire comme dans le panneau | Conforme |
| R33 | Élément étroit : la taille passe sous le nom, à l'intérieur du cadre | Sans fenêtre : « 40x90 » visible, sous le nom, dans le cadre, à son coin intérieur haut gauche ; la copie native la place au même endroit | Conforme (pas de capture en vraie fenêtre) |
| R34 | Le panneau ne montre jamais la disposition bulle | `report` : six bascules rapides, jamais la disposition bulle. `r34-apres-bascules.png` : panneau pleine hauteur avec en-tête, chat vide en bas, saisie « Sonnet 5.5 Moyen » | Conforme |
| R35 | Un brouillon par conversation, pas de puce du panneau dans la bulle | `report` : la puce du panneau n'est pas dans la bulle (0) | Conforme |
| R36 | (1) fondu universel de 250 ms après rechargement ; (2) glissé des valeurs quand seules des feuilles de style changent | (2) sans fenêtre : glissé de 120 à 360 avec 8 images intermédiaires, valeur finale juste, inspecteur qui suit image par image, repli « nomatch » vers (1), nettoyage fait. Code : `FadeMs = 250`, 150 ms en mouvement réduit. (1) en vraie fenêtre : **attesté seulement par le journal** (« the page shows them (transition) »), sans mesure image par image | Conforme pour (2). (1) à mesurer (**T6**) |
| R37 | Inspecteur, cadre et étiquettes collés pendant le défilement | Sans fenêtre : 40 images d'un défilement rapide, 0 décalage ; un changement de valeur glisse toujours. Vraie fenêtre (mes mesures sur `r37-00` à `03`, les seules où l'élément est visible) : la pastille « 6 » garde exactement le même décalage par rapport au cadre (+3 ; +9 px) sur chaque image, y compris pendant le mouvement | Conforme |
| R38 | Le diff s'ouvre en place, sans déplacer la bulle | `r38-diff-ouvert.png` + `report` : surface toujours à (748, 554) avec le diff ouvert, même décalage à l'élément ; elle grandit vers le bas | Conforme |

### Non-régression D1 à D8 et R13 à R25

| Point | Verdict | Preuve |
|---|---|---|
| D1, R14, R15, R16, R20 | Tiennent | v3 rejoué : deux étiquettes Inter au survol, retrait de 2,4 px au bord, inspecteur visible bulle ouverte, 320 px |
| D3 | **Corrigé**, avant : régression possible | v3 rejoué avec un profil vierge : aucun lanceur sans conversation disparue |
| D8 | **Corrigé** (décision appliquée) | `annotation-script.h` l. 158 : valeurs de padding et gap en Inter Display, blanc plein, 10/14 |
| T1 (R19) | **Fermé** | v3 rejoué : 2 513 pixels comparés, 0 changé |
| T2 (R25) | **Fermé** | v3 rejoué : la page **et le panneau** passent par 10 à 12 positions régulières (au plus 8 px d'un coup) |
| N1 (`1B8R-0`) | **Fermé** | Commit bf26dc0 : planche 3 mise à jour, comparée valeur par valeur, 0 écart non expliqué |
| R13, R24 | Tiennent | v3 rejoué |
| R18 | **Un contrôle en échec** | v3 rejoué : « a real click: the surface is on screen less than 100 ms later » → **215 ms**. La fenêtre apparaît bien une seule fois, à sa place (8 images, un seul coin de surface). C'est le délai entre le clic et l'apparition qui dépasse l'objectif de H |
| D2, D4, D5, D6, D7 | **Non rejoués après v3 et v4** (T3 toujours ouvert) | Derniers contrôles OK le 19 nov. (`comment-v2/host-report.json`, `switch-report.json`) |

### Restant

| # | Quoi | Correctif exact | Propriétaire |
|---|---|---|---|
| P1 | R18 : 215 ms entre le clic réel et l'apparition de la surface (objectif de H : moins de 100 ms) | Trouver ce qui retarde l'apparition (attente de l'accusé de la page, création de la région) et passer sous 100 ms, ou faire valider un autre objectif par l'utilisateur. Le placement et le glissé, eux, sont justes | H |
| O1 | `r26-survol-puce.png` : dans le panneau, juste après la bascule, le bas de la saisie affiche une étoile générique « ☆ Agent » (et « ☆ Nouveau chat » dans l'en-tête) ; `r34` montre ensuite « Sonnet 5.5 Moyen » avec le logo. R28 demande un bas de saisie juste dès la première image (décision écrite pour la bulle, même esprit pour le panneau) | Envoyer à la page du panneau, dès son ouverture, l'agent, le logo, le modèle et l'effort mémorisés, comme pour la bulle (R28). Ne jamais afficher « Agent » avec l'étoile quand ces valeurs sont connues | H (P si la page ignore ce qu'elle reçoit) |
| T3 | D2, D4, D5, D6 et D7 pas rejoués sur le build v4 | Rejouer le passage court v2 (bulle fermée, étape « Modifie … », conversation illisible, en-tête après rechargement, pas d'infobulle système) sur `build/chat-cv4` | H |
| T4 | R29 : l'exception « près du bas de la page » (un seul décalage, en douceur) n'est pas testée | Passage en vraie fenêtre sur un élément proche du pied de page : un seul déplacement vertical, doux, et la bulle entière | H |
| T5 | R31 en vraie fenêtre : curseur Windows réel et mode Chat à droite | Une capture du curseur (`GetCursorInfo`) avec Ctrl tenu, dans les deux modes, et un clic qui arrive au site | H |
| T6 | R36 niveau 1 : le fondu de 250 ms n'est prouvé que par le journal | Relever les statistiques de `ChatTransition` (`fadeFrames`, `fadeStart` / `fadeEnd`) dans le `report`, avec au moins une image intermédiaire capturée | H |

## Troisième passe — 20 novembre 2026 (R13 à R25, commits 986207e, 16fbf32, 06cfa61)

Sources :
- **H** : `build/chat-qa/comment-v3/` (`report.json`, 14/14 OK ; captures `r13` à `r25`, dont 12 images de l'ouverture pour R18).
- **P** : `build/chat-qa/comment-v2/panel/cv3/` (comparaison côte à côte avec Paper), `panel/ecarts.md` (2 040 valeurs, 0 écart non justifié) et `build/chat-qa/panel/report.json`.
- **Paper**, relu aujourd'hui pour `1AZV-0`, `1575-0` et `156O-0`.

Les décisions R13 à R25 de `chat-comment-v2-retours.md` priment sur Paper là où elles le disent.

### R13 à R25

| # | Décision | Preuve | Verdict |
|---|---|---|---|
| R13 | Bouton modèle de Paper (logo + modèle + effort), jamais « Claude Code » | `r13-bulle-modele.png` : « Sonnet 5.5 Moyen » avec le logo dès l'ouverture, avant tout message ; mode à gauche. `report` : « the bubble starts its agent at once » | Conforme |
| R14 | Même style d'étiquettes au survol qu'à la sélection | `r14-survol.png` : deux étiquettes Inter (« button.btn-primary » à gauche, « 240x44 » à droite), cadre débordant de 4 px, comme D1 | Conforme |
| R15 | Retrait de 2 px aux bords gauche et droit de la fenêtre | `r15-bord.png` + `report` : étiquettes à x = 2 et à 1 116 + largeur = 1 176 − 2, cadre et fond à x = 2 | Conforme |
| R16 | Padding et gap toujours visibles sur l'élément sélectionné, mises à jour en douceur | `r16-inspecteur-bulle-ouverte.png` : hachures de padding et pastille « 6 » visibles bulle ouverte (`inspector: 1`). Transition présente dans le code (`annotation-script.h` l. 899, interpolation). | Conforme, sauf le style des pastilles (**D8**, à trancher) |
| R17 | Une carte par message, coins selon la position (`1AZV-0`) | `cv3/R17-cartes-paper-vs-page.png` : puce modèle 12/12/12/4, 1re carte 4/10/4/4, carte du milieu 4 partout, barre 4/4/10/10, 2 px d'écart, fond `#FFFFFF14`. Valeurs de `1AZV-0` relues dans Paper aujourd'hui : identiques | Conforme (voir la note sur le relevé) |
| R18 | Apparition directement à sa place, glissé d'entrée plus rapide que le tiroir des Réglages | `report` : un seul rectangle de fenêtre, un seul coin de surface sur les 6 images où elle est visible, montrée à sa place après l'accusé de la page. `r18-ouverture-05` → `06` : fondu à la position finale, aucun saut. CSS : glissé de 12 px côté élément + fondu, 160 ms, sans agrandissement | Conforme |
| R19 | Rien ne bouge quand un menu ouvre la fenêtre | Le contrôle de H a comparé **0 pixel** (`comparedPixels: 0`), il ne prouvait rien. J'ai comparé moi-même `r19-surface-avant` et `-apres` : **0 pixel différent** hors du menu et de la puce survolée (barre du bas, bouton envoyer, bord droit) | Conforme (à corriger dans le test de H, voir plus bas) |
| R20 | Largeur 320 px | `r16` : surface de 398 à 797 = 400 px écran = 320 CSS ; `report` : 320. Placement inchangé : +15 px écran (+12 CSS) à droite du bouton | Conforme |
| R21 | Mode en icône seule | `r13`, `r16`, `cv3/R20-R21` : bouclier seul, carré de 24 px (`ecarts.md`) | Conforme |
| R22 | « + » ouvre directement le choix d'image | `panel.js` l. 830–831 : sans menu quand le dossier est sûr et que l'agent lit les images ; sinon le menu reste | Conforme (vérifié dans le code, pas de capture) |
| R23 | Chat vide : titre et suggestions en bas, au-dessus de la saisie | `build/chat-qa/panel/6.7a-vide.png` et `report` : 16 px au-dessus de la saisie. Paper `1575-0` : `justify-content: end`, padding 16, gap 16 ; suggestions avec gap 6, padding 10/12, rayon 10, bordure `#FFFFFF14` | Conforme |
| R24 | Plus de « Traiter les annotations en attente », jamais de doublon | `6.7a-vide.png` : deux suggestions seulement. `report` (H et P) : aucune suggestion « en attente », un `annotations.batch` de l'hôte est ignoré, aucun doublon | Conforme |
| R25 | Le bandeau glisse, la page suit sans saut | `report` : la page passe de 101 à 141 (et retour) en 5 ou 6 positions, au plus 20 px d'un coup ; aucun saut d'un coup | Conforme pour la page. **Le panneau** n'est pas mesuré : à ajouter au test |

### Non-régression D1 à D7

| # | Verdict | Preuve |
|---|---|---|
| D1 | Tient, en tenant compte de R16 | `r16` : cadre débordant de 4 px, fond 8 %, deux étiquettes Inter ; l'inspecteur est maintenant voulu (R16) |
| D2 | **Non revérifié** | Aucune capture bulle fermée dans `comment-v3` ; dernier contrôle : `comment-v2/host-report.json` du 19 nov. à 17 h 15, avant les commits v3 |
| D3 | **Régression possible** | Le lanceur en bas à droite est de nouveau visible sur `r14-survol.png` et `r15-bord.png`, et absent de `r13` et `r16`. D3 ne figure plus dans le `report` v3 |
| D4, D5 | Non revérifiés en v3 | Derniers contrôles OK le 19 nov. (`host-report`, `switch-report` de 18 h 03) |
| D6, D7 | Tiennent | `switch-report.json` (19 nov., 18 h 03) : en-tête « Sonnet 5.5 Moyen » après rechargement ; aucun attribut `title`, aucune fenêtre d'infobulle. `panel/report.json` (20 nov.) : « no system tooltip » sur tous les états |

### Écart signalé par H : texte des étiquettes en blanc plein au lieu de `#FFFFFFE6`

- **Paper** : `18QL-0` / `18QR-0`, texte 13/18 `#FFFFFFE6` sur `#3E67FF`.
- **Calcul** : `#FFFFFFE6` sur `#3E67FF` donne `#ECF0FF` composé, soit un contraste de **4,02:1**. Le blanc plein donne **4,58:1**. Pour du texte de 13 px non gras, le minimum WCAG AA est 4,5:1 (skill better-accessibility, obligatoire pour ce chantier).
- **Verdict** : écart **admis**. La valeur de Paper ne passe pas le contraste minimum. Le blanc plein est le plus petit changement qui le passe, sans toucher au fond ni à la taille.
- **À faire** : demander à l'utilisateur de reporter `#FFFFFF` dans Paper sur les étiquettes, pour que le design et le code redeviennent identiques.

### Défauts et points restants

| # | Quoi | Correctif exact | Propriétaire |
|---|---|---|---|
| D3 | Le lanceur en bas à droite réapparaît sur `r14` et `r15` alors qu'aucune conversation n'a perdu son élément (ou alors le profil de test en garde une d'un passage précédent) | Remettre le contrôle D3 dans le passage v3, avec un profil vierge. Si le lanceur s'affiche sans conversation disparue, le masquer comme en v2 (commit 827debf). Faire une capture `d3-*` | H |
| D8 | R16 demande les valeurs de padding et de gap « avec le style des étiquettes de Paper ». Les pastilles sont restées en JetBrains Mono 10/14, coins 3 px de tous les côtés (`annotation-script.h` l. 145) | **À trancher par l'utilisateur.** Ma proposition : police Inter Display, celle des étiquettes `18QK-0`, texte blanc plein, taille 10/14 gardée parce qu'une bande de padding peut ne faire que 4 px. Une pastille de 20 px de haut comme `18QK-0` ne tiendrait pas dans la bande | H |
| T1 | Le test R19 de H compare 0 pixel (`comparedPixels: 0`), donc il ne prouve rien même quand il passe | Comparer pour de vrai les pixels hors du rectangle du menu et de son ombre, et faire échouer le test si `comparedPixels` vaut 0 | H |
| T2 | R25 : seul le mouvement de la page est mesuré, pas celui du panneau | Mesurer aussi le haut du panneau sur les mêmes images (pas plus de 20 px d'un coup, aucun saut) | H |
| T3 | D2, D4 et D5 ne sont pas rejoués après les commits v3 | Rejouer le passage court v2 (bulle fermée, étape, conversation illisible) sur le build v3 | H |
| N1 | Mon relevé `chat-comment-paper-values.md` est en retard pour la planche 3 : l'utilisateur a ajouté la carte `1B8R-0` (4/10/4/4) et `1B06-0` est passée à 4 partout. Les valeurs à jour sont données ci-dessus (R17) ; `fidelity.json` de P ne relie pas encore `1B8R-0` | Ajouter `1B8R-0` à `tests/chat-panel/fidelity.json` (état « 2 messages ») | P |

## Contre-audit — 19 novembre 2026, soir (hébergement fenêtré, commits jusqu'à 100bb84 ; D4 corrigé par P en 22d4d58)

Sources : nouvelles captures en vraie fenêtre de H (`d1-*`, `d2-*`, `s1`…`s3-*`, `r3-*-fenetre.png` / `r3-*.png`, `r9-*`, `r10-bandeau.png`, `probe-fenetree-bulle.png` / `-coin.png`), plus `host-report.json` (32/32 OK) et `switch-report.json` (12/12 OK). Mesures au pixel sur les images, à l'échelle 125 %.

### D1 à D5

| # | Statut | Preuve (vraie fenêtre) |
|---|---|---|
| D1 | **Corrigé** | `d1-bulle-ouverte-session-prete.png` : le bouton va de 433 à 733 et le cadre de 428 à 737 / 554 à 618, soit un débord de 5 px écran = 4 CSS. Le fond dans le cadre est rgb(230, 233, 244), c'est-à-dire `#3E67FF14` sur `#F4F4F3`. Deux étiquettes Inter séparées, « button.btn-primary » et « 240x44 » (vraie taille du bouton de test), hautes de 25 px écran (20 CSS), posées sur le haut du cadre (529 → 554) et alignées sur ses bords gauche et droit. Pas d'inspecteur, pas d'icône. Encore vrai après rechargement (`s1-d1-apres-rechargement.png`, `switch-report` « D1 after the reload »). |
| D2 | **Corrigé** | `d2-agent-au-travail-bulle-fermee.png` : cadre seul (débord de 4 CSS, sans fond : pixel intérieur rgb(244, 244, 243)) + icône `19NA-0` collée au coin haut gauche. Pas d'anneau, pas d'étiquette, pas d'inspecteur. `host-report` : D2 au travail et D2 au repos sont `spinner: 0, labels: [], inspector: 0`. |
| D3 | **Corrigé** | Plus aucun lanceur en bas à droite sur `d1`, `d2`, `r3-*`, `s1`, `s3`. `host-report` : « no conversation whose element is gone: no launcher » et « the element is in the page: no launcher, no activity on it ». |
| D4 | **Corrigé** (P, 22d4d58) | `host-report` (R9) : étape « Modifie checkout.css… » ; infobulle Historique « A modifié checkout.css » (`r3-infobulle-historique.png`). Verbe en français, nom du fichier seul, « … » pendant l'étape. |
| D5 | **Corrigé** | `host-report` et `switch-report` : « its conversation unreadable: the bubble opens anyway, on a new conversation », `ok` (`s3-d5-nouvelle-conversation.png`). |

### Ce qui dépend de l'hébergement fenêtré (revérifié)

| Point | Paper | Vraie fenêtre | Écart |
|---|---|---|---|
| Coins (R1) | rayon 16, sans liseré | `probe-fenetree-coin.png` et `host-report` : le coin laisse voir la page (244, 244, 243), surface (23, 23, 23) = `#171717` ; `DWMWCP_DONOTROUND` | 0 |
| Ombre / bordure | aucune | Le pixel juste à côté de la surface est celui de la page | 0 |
| Largeur / placement | 305 ; +12 / −4 | Fenêtre de 381 px écran = 305 CSS. `d1` : bouton jusqu'à 733, bulle à 748 → +15 écran = +12 CSS ; haut à 554 contre 559 → −4 CSS | 0 |
| Menu modèle hors de la bulle (R3) | `17US-0` 300 px, 6 px au-dessus, bord droit aligné | `r3-sous-menu-effort.png` : menu de 700 à 1073 (≈ 300 CSS), bord droit aligné sur la puce du modèle, au-dessus d'elle ; il sort de la bulle à gauche, la page reste visible autour. La fenêtre revient à la surface après Échap | 0 |
| Sous-menu Effort | `17VU-0` à côté, 214 px, hauts alignés | De 1079 à 1347 (268 écran = 214 CSS), 6 px écran (≈ 4 CSS) à droite du menu modèle, hauts alignés (575 / 575), ligne choisie avec fond et coche | 0 |
| Menu des modes | `14RK-0` 290 px, ombre, 4 lignes | `r3-menu-mode.png` : bord gauche aligné sur « Demander », au-dessus du bouton, sort de la bulle à droite, ombre visible, les 4 lignes à la lettre | 0 |
| Menu « + » | « Joindre une image » seul (R5) | `r3-menu-plus-fenetre.png` : une seule ligne, au-dessus du « + » | 0 |
| Infobulle Historique | hors de la bulle si besoin | `r3-infobulle-historique.png` : au-dessus de « Historique », elle sort de la bulle à droite, rien n'est coupé | 0 |
| Masque du bandeau (R10) | `1AYJ-0` | `r10-bandeau.png` : la trame s'éteint avant « Aucun agent connecté », le sélecteur et Quitter, comme avant | 0 |

### Défauts restants

| # | Planche | Paper | Vraie fenêtre | Correctif exact | Propriétaire |
|---|---|---|---|---|---|
| D6 | 3 `1AWI-0` (en-tête de la réponse) | « Sonnet 5.5 Élevé » avec le logo à gauche (`1AZX-0` : logo `1AZZ-0`, nom `1B01-0` ; R12) | `s1-d1-apres-rechargement.png` : après le rechargement de la page, l'en-tête de la réponse dit « Claude Code » (nom de l'agent) au lieu du modèle et de l'effort, alors qu'il disait « Sonnet 5.5 Moyen » avant (`d1`). | Conserver le modèle et l'effort de chaque tour et les renvoyer avec le tour quand le fil est redonné à la page (rechargement, réouverture). La page n'affiche le nom de l'agent que si le tour n'a vraiment pas de modèle. | H (P si la page perd une valeur qu'elle reçoit) |
| D7 | 3 `1AWI-0` (bas de la saisie) | Aucune infobulle système. Les infobulles de la bulle sont dessinées (`1B7H-0` : fond `#2F2F2F`, coins 10/10/10/4) | `r3-infobulle-historique.png` : sous le « + », une infobulle **système** « Joindre » (carré noir, liseré blanc, police système), venue de l'attribut `title` du bouton. Elle déborde sous la bulle. | Dans la bulle, retirer `title` des boutons de la barre (« + », mode, modèle, envoyer ; `aria-label` reste pour les lecteurs d'écran). Si une infobulle est voulue, utiliser l'infobulle dessinée, comme celle du bouton envoyer (§20.1). | P (`assets/chat-panel/panel.js`, l. 273 et les autres `title:` de la barre) |

### À vérifier, pas compté comme défaut

- **Liseré autour de « Quitter Échap »** dans `r10-bandeau.png`, absent dans `d1`, `d2`, `r3` : c'est sans doute l'état survol ou focus au moment de la capture. Paper ne dessine pas cet état.
- **Contour blanc sur la ligne « Sonnet 5.5 »** du menu modèle (`r3-sous-menu-effort.png`) et **contour bleu sur « + »** (`r3-infobulle-historique.png`) : focus clavier, car le test ouvre les menus au clavier. Paper ne dessine pas le focus, et il est nécessaire pour l'accessibilité.
- **Bas de la saisie avant que la session soit prête** (`probe-fenetree-bulle.png` : puce « Claude Code » au lieu de mode + modèle) : Paper n'a pas de planche pour cet état. Une fois la session prête (`d1`), c'est conforme.
- Les variantes entre planches V1 à V4 et la pastille « BETA » attendent toujours la décision de l'utilisateur (voir plus bas).

## Premier audit (matin du 19 novembre) — historique

## Résultat

**La bulle elle-même est fidèle à Paper.** Les points vérifiés en vraie fenêtre sont tous conformes : coins, absence d'ombre et de bordure, fond, largeur, placement +12 / −4, menus qui sortent de la bulle, sous-menu Effort à côté, masque du bandeau. La page a 0 écart non justifié sur 2 076 valeurs.

**Il reste 5 défauts, presque tous autour de la bulle** : marques sur l'élément visé, icône pendant le travail, lanceur en bas à droite, libellé de l'étape en cours, et un test de l'hôte en échec. Ils sont listés plus bas.

## Défauts (à corriger)

| # | Planches | Paper | Code (vraie fenêtre) | Correctif exact | Propriétaire |
|---|---|---|---|---|---|
| D1 | 1 `18NS-0`, 2 `1B30-0`, 2.1 `1AIE-0` / `1AE7-0`, 2 `1ANU-0` / `19ON-0`, 2.2b `1AAE-0` / `1A6L-0`, 3 `1AWI-0` / `19IO-0` (bulle ouverte) | **Cadre** `18QJ-0` : 2 px `#3E67FF`, fond `#3E67FF14`, **déborde de 4 px** de chaque côté (528×52 pour un bouton 520×44), sans rayon. **Deux étiquettes séparées** au-dessus (y −24 par rapport à l'élément, hauteur 20, padding 0 6, coins hauts 3 px, fond `#3E67FF`, Inter Display 13/18 400 `#FFFFFFE6`) : à gauche le sélecteur (`left −4`), à droite la taille (`right −4`, format `520x44`). **Ni icône, ni hachures, ni pastille de valeur.** | `r2-bulle-ouverte.png` : le cadre est posé **sur** les bords du bouton (bleu de x 83 à 382 = bords du bouton, aucun débord). Il y a **une seule** étiquette en police à chasse fixe, « button.btn-primary · 240×44 ». Les **hachures de padding** et la **pastille « 6 »** de l'inspecteur restent visibles. Dans `r6-reouverture.png`, `r9-termine.png` et `r9-autorisation-apres-reouverture.png`, l'icône de la planche 4 (ou un anneau qui tourne) est déjà sur l'élément alors que la bulle est ouverte. | Tant que la bulle est ouverte sur un élément : masquer l'inspecteur (hachures, union des gaps, pastilles de valeur) ; dessiner le cadre de `18QJ-0` (`inset −4px`, bordure 2 px `#3E67FF`, fond `#3E67FF14`) ; dessiner deux étiquettes identiques à `18QK-0` / `18QQ-0` (Inter Display 13/18, texte `#FFFFFFE6`, padding 0 6 px, hauteur 20, coins hauts 3) collées au haut du cadre, la gauche avec le sélecteur, la droite avec la **vraie** taille au format `LxH` (« 520x44 », x minuscule comme dans Paper) ; pas d'icône. L'icône `19NA-0` n'apparaît que bulle fermée (planche 4). | H (`src/`, cadre et inspecteur du mode Annotation) |
| D2 | 4 `18IJ-0` (bulle fermée, agent en cours) | Cadre 2 px `#3E67FF`, **sans fond**, débord 4 px, **sans étiquette** ; icône `19NA-0` 24×24, fond `#3E67FF`, coin bas droit 6 px, à (−1,4 ; −1,4) dans le cadre, glyphe « Annoter » blanc. Brief règle 4 : « la bordure + l'icône, y compris pendant que l'agent travaille ». | `r9-bulle-fermee-autorisation-en-attente.png` : l'icône est remplacée par un **anneau qui tourne**. L'étiquette « button.btn-primary · 240×44 », les hachures et la pastille « 6 » sont visibles (à confirmer : le pointeur était peut-être sur l'élément). Bulle fermée et agent arrêté (`r9-termine` après fermeture) : à vérifier sur une capture sans pointeur au-dessus. | Bulle fermée : cadre seul (`inset −4px`, 2 px `#3E67FF`, sans fond) + icône `19NA-0` telle quelle, **sans anneau**, que l'agent travaille ou non. Pas d'étiquette ni d'inspecteur hors survol. Si un signe d'activité est voulu, il doit d'abord être dessiné dans Paper. | H |
| D3 | toutes (aucune planche ne montre de lanceur) | Aucun lanceur en bas à droite sur les 11 planches. Brief règle 6 : l'historique en bas à droite « ne sert plus qu'aux conversations dont l'élément a disparu ». | Le lanceur est affiché en permanence (`r2`, `r10`, `r9-termine`). Dans `r9-autorisation-apres-reouverture.png`, il porte un anneau et une pastille « 1 » pour la conversation **de l'élément encore présent**. | En Mode Commentaire : lanceur caché tant qu'aucune conversation n'a perdu son élément. Il ne compte et ne signale que ces conversations-là ; il n'affiche aucune activité d'une conversation ancrée sur un élément visible. | H |
| D4 | 2 `1B30-0`, 2.1 `1AIE-0`, 2 `1ANU-0` (étape en cours, étapes au survol) | « Lit checkout.css… » : verbe français, **nom du fichier seul**, « … » tant que l'étape tourne. Au survol : « A lu index.html », « A lu Checkout.tsx », « A utilisé 1 outil MCP ». | Le titre brut de l'agent est repris tel quel : « Modifier styles/checkout.css » (faux agent, `r9-autorisation-apres-reouverture.png`), et « Edit styles\landing.c… » avec le vrai Claude (retour R8). Anglais, chemin complet, sans « … ». | Composer le libellé avec `kind` + nom de fichier de `locations[0].path` : read → « Lit X… » / « A lu X » ; edit → « Modifie X… » / « A modifié X » ; execute → « Exécute … » / « A exécuté … » ; MCP → « Utilise un outil MCP… » / « A utilisé N outil(s) MCP ». Repli sur `title` seulement si `kind` est inconnu, en ajoutant « … » pendant `in_progress`. Même règle dans le panneau. | P (`assets/chat-panel/panel.js`) ; H si l'on préfère traduire les titres dans l'hôte |
| D5 | règle 4 / R6 (rouvrir la conversation d'un élément) | Un clic sur l'élément ouvre toujours la bulle. | `host-report.json` : étape « R6 · its conversation unreadable: the bubble opens anyway (a new conversation) » en **échec** (`ok: false`, « 1 file(s) removed »). | Si la conversation enregistrée est illisible, ouvrir quand même la bulle sur une nouvelle conversation pour cet élément, puis relancer le parcours jusqu'à `ok: true`. | H |

## Conforme en vraie fenêtre (vérifié)

| Point | Paper | Mesure en vraie fenêtre | Écart |
|---|---|---|---|
| Largeur de la bulle | 305 | Fenêtre de 381 px écran = 305 CSS (`host-report` : rect 398–779) ; surface `#app` 305 (DOM) | 0 |
| Fond | `#171717` | rgb(23, 23, 23) mesuré dans `r2` | 0 |
| Coins (R1) | rayon 16 | Courbe pleine largeur atteinte en 20 px écran (16 CSS), arrondi lisse ; `DWMWCP_DONOTROUND`, `WS_EX_NOREDIRECTIONBITMAP` ; aucun pixel noir aux coins | 0 |
| Ombre / bordure | aucune | Le pixel juste après le bord droit et le bord bas est le fond de la page (`#F4F4F3`) ; aucun liseré | 0 |
| Placement (règle confirmée) | bord droit élément +12, haut élément −4 | `r2` : bouton 83–382, bulle à x 398 → +15 px écran = +12 CSS ; haut 554 contre 559 → −5 = −4 CSS. `r9-termine` : +14 px écran (arrondi < 1 px CSS), −5 | 0 |
| Hauteur au repos | contenu | 105 px écran = 84 CSS (12 + 20 + 12 + 28 + 12) | 0 |
| Menus hors de la bulle (R3) | 6.4 `17US-0`, `14RK-0` | `c-menu-modele-fenetre`, `c-menu-mode-fenetre`, `c-menu-plus-fenetre` : menus au-dessus de leur bouton, qui sortent de la bulle, rien de rogné. Repli dessous quand la place est refusée (`c-menu-refuse-fenetre`) | 0 |
| Sous-menu Effort | `17VU-0` à côté de `17US-0` | `c-menu-effort-gauche-fenetre` : 214 px, 4 px du menu modèle, hauts alignés (à gauche faute de place à droite, comme le prévoit le contrat §20.1) ; « Autres modèles › » de même (`c-menu-autres-fenetre`) | 0 |
| Menu des modes | `14RK-0`, 4 lignes | Les 4 lignes, textes à la lettre, ligne choisie avec fond et coche, ombre `#00000080 0 16 48` (`c-menu-mode-fenetre`, 133 valeurs DOM égales) ; ordre vérifié par l'hôte | 0 |
| Infobulle des étapes | `1B7H-0` 178×82 | Hors de la bulle, au-dessus de l'étape (`c-2.1-survol-fenetre`) ; décalage de 2 px expliqué plus bas | voir V2 |
| Masque du bandeau (R10) | `1AYJ-0` | `r10-bandeau` comparé à la planche 3 : la trame s'éteint avant la zone du sélecteur et de Quitter, au même endroit que dans Paper ; rien ne passe sous le sélecteur ni sous Quitter | 0 |
| Sélecteur Commentaire / Panneau | `1AYP-0` | Deux boutons 24, l'actif sur fond blanc 10 %, placé avant Quitter | 0 |
| R2 (rien avant l'envoi) | — | `r2-rien-apres-fermeture` + `host-report` : aucune marque après fermeture sans envoi | 0 |
| R9 (autorisation dans la bulle) | `1ANU-0` | Carte présente après réouverture ; premier texte 120 ms (bulle) contre 99 ms (panneau) ; fin de tour 316 ms contre 304 ms | 0 |
| R12 (logo dans l'en-tête des réponses) | `1B66-0` | Logo Claude 14 px à gauche de « Sonnet 5.5 » (`r9-termine`) | 0 |

## Écarts déclarés justifiés par P (`ecarts.md`, 28 valeurs) — mon avis

| Groupe | Valeurs | Avis |
|---|---|---|
| Bouton du mode plus large (53 → 93,8 px) | `18TZ-0`, `1B6M-0`, `1AM0-0`, `1AHZ-0`, `1B0U-0`, `1B2G-0` | **Admis** : « Demander » au lieu de « Ask » (brief règle 7). Conséquence normale : le nom du modèle se coupe en premier quand la place manque (règle de Paper 6.4), comme dans `r9-termine` (« Sonnet … Moyen »). |
| Position du premier caractère < 2 px | `1B6Y-0`, `1B7L-0`, `1B7P-0`, `1B7R-0`, `1ARJ-0`, `1ANE-0`, `1B1E-0` | **Admis** : même police, rendu de Chromium contre rendu de Paper. Les boîtes sont égales. |
| Marges portées par l'enfant (zone du diff, barre « 1 fichier modifié ») | `1AS9-0`, `1B1J-0`, `1B08-0` | **Admis** : positions des lignes mesurées égales, rendu identique. |
| Variantes entre planches | V1 : bulle de 220 px dans `1AE7-0` (210 ailleurs) ; V2 : étape survolée de `1AIE-0` 2 px plus à droite que dans `1B30-0` ; V3 : anneau 12 px collé au bord dans `1ANU-0` (14 px en retrait ailleurs) ; V4 : rayon 10 de la bulle utilisateur dans `1AWI-0` (12 ailleurs) | **Admis sous réserve** : Paper se contredit d'une planche à l'autre, il faut une seule valeur, et P a retenu la majorité. Ce n'est pas décidé dans les retours : **à faire valider par l'utilisateur**, ou à corriger dans Paper pour que les planches s'accordent. |

## Hors comparaison (pas des défauts du Mode Commentaire v2)

- **Pastille « BETA »** à droite de « Mode Annotation » : décision antérieure du mode Annotation (`src/annotation-accent.h`), absente des planches. À confirmer avec l'utilisateur si les planches doivent la montrer.
- **« Aucun agent connecté · Connecter »** dans le bandeau : rappel du serveur d'annotations, normal en environnement de test (aucun client connecté). Dans Paper ce rappel est vide (`1AYN-0`, hauteur 0).
- **Bas de la saisie dans `r2`** (« Claude Code » au lieu de mode + modèle) : la session n'était pas encore prête au moment de la capture. Les valeurs DOM de P pour cet état (`c-1-saisie`, 77 valeurs) sont égales à Paper. Une capture en vraie fenêtre avec la session prête fermerait ce point.
- **Bordure autour de « Quitter »** dans `r10` (absente dans `r2`) : probablement l'état focus ou survol au moment de la capture. À vérifier.
- **Haut du fil estompé** (`1AS0-0`) : n'apparaît que lorsque le fil défile (contrat §19). Dans `r9-autorisation` le fil tient sans défiler, donc pas de masque. Même dégradé que Paper (`#292929`, 10,75 % → 98,04 %).

## Reste à faire

1. H corrige D1, D2, D3 et D5. P (ou H) corrige D4.
2. Nouvelles captures en vraie fenêtre de H, chacune mise côte à côte avec la planche Paper :
   - bulle ouverte et bulle fermée, avec le pointeur hors de l'élément ;
   - session prête avant l'ouverture ;
   - agent au travail bulle fermée.
3. Faire valider par l'utilisateur les variantes V1 à V4 et la pastille « BETA ».
