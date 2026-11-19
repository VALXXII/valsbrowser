# Mode Commentaire v2 — audit de fidélité à Paper

Audit du 19 novembre 2026 (worker F). Référence : `design/chat-comment-paper-values.md` (valeurs lues dans Paper, commits c4e6a46 et 0c9d129) et les captures Paper de `build/chat-qa/comment-v2/paper/`. Décisions admises : `chat-comment-v2-retours.md` (R1 à R12), réponses du coordinateur (4 modes de `14RK-0`, placement +12 / −4, vraie taille dans l'étiquette), brief règle 7 (« Demander » au lieu de « Ask »).

Ce qui a été examiné :
- **Vraie fenêtre, côté hôte (H)** : `build/chat-qa/comment-v2/r*.png` et `host-report.json`. Mesures au pixel sur les images (échelle 125 % : 1 px CSS = 1,25 px écran).
- **Vraie fenêtre, côté page (P)** : `panel/*-fenetre.png`, `panel/c-menu-*.png`.
- **Valeurs DOM (P)** : `panel/ecarts.md`, `tests/chat-panel/fidelity.json`, soit 2 076 valeurs comparées nœud par nœud au relevé Paper, avec 28 écarts déclarés justifiés. J'ai contrôlé chaque justification ci-dessous.

Aucune vraie fenêtre n'a été lancée de mon côté.

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
