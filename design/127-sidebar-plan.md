# #127 — Panneau latéral (variante A finale) : plan

Branche `VALXXII/127-sidebar` (depuis `origin/main` 0d97f82). Maquette : Paper « Browser Design », page
« #127 — Panneau latéral (A finale validée) » (`p-X-0`), planches « A finale 01 » à « 16 ». Carte de l'existant :
`design/etat-actuel-interface.md` (branche `VALXXII/etat-actuel-interface`). Les valeurs (tailles, couleurs, durées) ne
sont **pas** dans ce plan : elles sont lues dans Paper (`get_jsx`, `get_computed_styles`) au moment de coder et
rangées dans `src/sidebar.h`, avec leur nœud Paper en commentaire.

## 1. Ce qui change

Variante A = **une seule colonne à gauche** remplace la rangée d'onglets et la barre d'outils. Dans la colonne, de
haut en bas : bouton de profil, Précédent / Suivant (segment) + Actualiser, bouton « Plier le panneau » ; champ
d'adresse (Copier, étoile) ; favoris en grille ; filet ; onglets (liste verticale) + « Nouvel onglet » ; en bas les
outils Annoter · Historique · Téléchargements · Réglages. À droite de la colonne, une bande de fenêtre (boutons
Réduire / Agrandir / Fermer) puis la page, en carte.

## 2. États

| État | Planche | Colonne | Page | Adresse |
| --- | --- | --- | --- | --- |
| **Complet** | 01, 04, 05, 06 | large, docké, pousse la page | à droite de la colonne | dans la colonne |
| **Compact** | 02 | étroite (icônes seules), dockée | à droite de la colonne étroite | dans la bande du haut, au-dessus de la page, avec Précédent / Suivant / Actualiser |
| **Compact survolé** | 03 | après le délai de survol, la colonne complète s'ouvre **par-dessus** la page | ne bouge pas | dans la colonne ouverte |
| **Tiroir ouvert** | 07–12 | inchangée, le bouton du tiroir est marqué | inchangée | inchangée |
| **Focus repos / survol / tiroir** | 13–15 | cachée ; bande sensible au bord gauche ; au survol, la colonne complète glisse par-dessus la page | toute la largeur | — |
| **Glisser un onglet** | 05 | onglet soulevé qui suit la souris, place d'origine en creux, trait d'insertion | — | — |
| **Nouvel onglet** | 06 | ligne « Nouvel onglet » active, suggestions sous l'adresse (plus larges que la colonne) | page vide | en saisie |

Passage complet ↔ compact : la largeur s'anime, les titres s'effacent d'abord (durées dans la planche 16).
Mouvement réduit : changement immédiat (fondu court), comme le reste de l'app.

## 3. Architecture

**Modèle pur `src/sidebar.h`** (C++17 sans Win32, testé par `tests/sidebar_test.cpp`) :
- `Mode { Complete, Compact }`, `Presentation { Docked, Overlay, Hidden }` ;
- état : mode, survol en cours (horodatage d'entrée, délai avant ouverture, fermeture à la sortie), largeur animée,
  Focus actif (fourni par l'hôte, `false` sur `main`) ;
- géométrie (DIP) : largeur de colonne, boîte de la page, bande du haut, haut des tiroirs, rangées de la colonne
  (haut, adresse, grille de favoris, liste d'onglets avec défilement, outils) ; tout ce que le dessin et le test de
  clic lisent vient de ces fonctions, pour qu'ils ne divergent jamais (même principe que `Design::TabStrip`).

**Réglage** : clé `sidebar=complete|compact` dans `src/settings.h` (`State::sidebarCompact`), écrite / relue
comme les autres, valeur par défaut « complet ». Test dans `tests/settings_test.cpp`.

**Raccourci** : `Ctrl+Maj+,` (`VK_OEM_COMMA`) → commande `SidebarToggleCommand` dans `Key()` (`src/main.cpp`),
même chemin que le bouton « Plier / Déplier le panneau ». Ajouté au panneau Ctrl+/ (`src/shortcuts-panel.h`,
section Onglets) et à `tests/shortcuts_test.cpp`. Textes « Plier le panneau » / « Déplier le panneau »
(« Collapse sidebar » / « Expand sidebar ») dans `src/i18n-catalog.inc`, vérifiés par `tools/i18n/check_strings.py`.

**Dessin** : `Chrome` (`src/chrome.h`) garde un seul peintre Direct2D pour toute la fenêtre. `Build()` choisit
`BuildSidebar()` (régions de la colonne, mêmes identifiants de commandes qu'aujourd'hui : `'B' 'F' 'R' 'L' 'C' 'K'
'Y' 'D' 'S' 'N'`, `TabBase+i`, `CloseBase+i`, `BookmarkBase+i`, `'T'`, `900` profil, plus le bouton de pliage).
Le champ d'adresse reste le vrai champ Windows (`address`), placé par `Layout()` sur `chrome.edit` (aujourd'hui les
ordonnées 44–76 y sont écrites en dur : elles passent par la géométrie). Les menus (onglet, favoris, profils),
l'info-bulle, le focus clavier (`FocusOrder`) et l'accessibilité (`src/accessibility.h`) suivent les régions.

**Page** : `ContentBox()` (`src/main.cpp`) prend sa gauche et son haut de `Sidebar::PageBox()` au lieu de
`Viewport::inset` / `chrome.ContentTop()`. La vue partagée, la barre de recherche, les modales de page, les toasts,
la bulle d'aperçu de lien et la scrollbar suivent déjà `ContentBox()`. Les coins de la page sont lus dans Paper
(masques d'angle de `Viewport`).

**Tiroirs = les vrais** : Téléchargements, Historique, Favoris, Réglages, Son des sites restent les fenêtres
d'aujourd'hui (`src/download-panel.h`, `src/history-panel.h`, `src/bookmarks-panel.h`, `src/settings-panel.h`,
`src/audio-panel.h`), ancrées à droite. Seul leur **haut** change : la constante `DrawerLayout::top` (81) devient
une valeur fournie par l'hôte (`Design::Vertical::DrawerTop()`), égale au haut de la page. Les boutons du bas de la
colonne appellent les mêmes commandes (`ToggleHistory`, `ToggleDownloads`, `ToggleSettings`) ; Ctrl+Maj+O garde
le tiroir Favoris. Le bouton du tiroir ouvert reste marqué (`chrome.latched`).

**Colonne par-dessus la page** (compact survolé, Focus) : les pages sont des fenêtres enfants qui couvrent la
fenêtre principale. Pendant que la colonne est ouverte par-dessus, chaque fenêtre de page reçoit une région sans
le rectangle de la colonne (même technique que la branche Mode Focus) : la fenêtre principale dessine la colonne
à cet endroit et reçoit la souris, la page ne bouge pas et n'est pas redimensionnée.

## 4. Mode Focus (branche `VALXXII/Mode-Focus`, pas dans `main`)

Le panneau doit compiler et marcher sur `main` seul. Point d'intégration : `Sidebar::State::focus` (toujours
`false` sur `main`) et `Presentation`. Ce que le mode Focus devra faire en arrivant :
- mettre `focus=true` : la colonne passe en `Hidden`, la page prend la largeur (boîte Focus de `focus-mode.h`) ;
- remplacer son panneau flottant du haut par la colonne : bande sensible au **bord gauche** (planche 13) au lieu
  du bord haut ; après le délai, `Overlay` (planche 14) — la même colonne et la même découpe des pages que le
  compact survolé ; elle repart quand la souris la quitte ;
- tiroirs (planche 15) : ouverts au clavier ou depuis la colonne survolée, à droite comme ailleurs, colonne cachée ;
- Ctrl+Maj+, en Focus : à décider (voir questions).

## 5. Tests

Sans fenêtre (journée) : `tests/sidebar_test.cpp` (géométrie, états, survol, pliage, persistance), `settings_test`,
`shortcuts_test`, `i18n_test` + `check_strings.py`, rendu hors écran de la colonne dans une image (comme
`design/etat-actuel-interface/capture`) comparé aux exports Paper, build complet. Fenêtre réelle (ouverture,
glisser, survol par-dessus la page, tiroirs) : liste pour la tournée de nuit de Fix.

## 6. Questions ouvertes

1. **Remplacement ou option ?** La colonne remplace-t-elle les onglets en haut pour tout le monde, ou faut-il un
   réglage pour garder l'ancienne barre ?
2. **Réglage du mode** : complet / compact seulement par le bouton et Ctrl+Maj+, (mémorisé), ou aussi une ligne
   dans Réglages › Affichage ?
3. **Barre de favoris** : que deviennent Ctrl+Maj+B et le réglage « Barre de favoris » (Automatique / Toujours /
   Jamais) avec la grille ? Grille de plus de 5 favoris : plusieurs rangées, défilement, ou bouton « autres » ?
   Un dossier dans la grille : icône dossier qui ouvre le menu ?
4. **Beaucoup d'onglets** : la liste défile-t-elle (avec un fondu) ? Et en compact ?
5. **Nouvel onglet** (planche 06) : un onglet vide est-il une ligne à part, ou la ligne « Nouvel onglet » devient-elle
   l'onglet actif jusqu'à ce qu'on tape une adresse ?
6. **Bandeau du mode Annotation** (aujourd'hui sous la barre d'outils) : où va-t-il ? Au-dessus de la page ?
7. **Bouton « Son des sites »** (quand un site fait du son) : où est-il dans la colonne ? Et la puce d'autorisation
   d'un site dans l'adresse étroite ?
8. **Téléchargements** : le bouton ouvre-t-il le tiroir directement (planche 07) ou d'abord le petit popover des 4
   derniers, comme aujourd'hui ?
9. **Glisser un onglet** (planche 05) : lâché sur la grille → devient un favori (nouveau) ; lâché hors de la fenêtre
   → nouvelle fenêtre (n'existe pas aujourd'hui) ; lâché sur la page → vue partagée comme aujourd'hui ?
10. **Focus, planche 15** : le texte dit « le tiroir se colle au bord gauche » mais le dessin le met à droite. Lequel ?
    Et Ctrl+Maj+, pendant Focus ?
11. **Fenêtre étroite** : sous quelle largeur la colonne passe-t-elle d'elle-même en compact (la page n'aurait
    plus que ~310 DIP à 560) ?
12. **Textes de la planche 16 sans dessin** : « Effacer l'historique… » et « Ouvrir le dossier » — où apparaissent-ils ?
13. **Anneau de focus** : la planche 16 dit « anneau accent 2 px, intérieur », l'app utilise partout 1 DIP blanc
    (10 % / 50 %). On change pour la colonne seulement ?

### Réponses (24 nov.) et ce que fait la version d'essai

| # | Réponse | Dans le code |
| --- | --- | --- |
| 1 | **Val (24 nov.) : la barre du haut reste le défaut** (anciens profils et nouvelles installations) ; le panneau latéral est une option à activer (ligne de Réglages dessinée par le Designer) | réglage `layout=top-bar\|side-panel` (`settings.sidePanel`, faux par défaut), appliqué à chaud sans redémarrage (`ApplyLayout`, `Change::SidePanel`) ; `browser.exe --side-panel` montre le panneau pour un lancement d'essai sans rien enregistrer |
| 2 | Bouton + Ctrl+Maj+, mémorisé, pas de ligne de Réglages | clé `sidebar=complete\|compact` (`src/settings.h`) |
| 3 | La grille remplace la barre de favoris ; Ctrl+Maj+B et Automatique / Toujours / Jamais s'y appliquent ; 2 rangées max, la 10ᵉ tuile devient « » » ; compact : 5 puis « » » ; dossier = menu natif ; Toujours + 0 favori : tuile pleine largeur étoile + « Ajouter cette page » + Ctrl D | `Chrome::Grid`, `BuildSidebar` (mêmes régions et menus que la barre) |
| 4 | La liste défile avec un fondu, complet et compact | molette sur la liste, fondus de 24 DIP, l'onglet choisi revient dans la vue |
| 5 | Un nouvel onglet est une vraie ligne (globe, « Nouvel onglet », active) au-dessus de « + Nouvel onglet » | `sideHome` : globe + « Nouvel onglet » |
| 6 | Bandeau Annotation en haut de la zone page, sur la largeur de la page | `Input::band`, `annotation-mode.h BandRect` |
| 7 | Son des sites dans les outils du bas entre Annoter et Historique ; pastille d'autorisation dans le champ | outils `N V Y D S` ; la pastille suit le champ |
| 8 | Popover des 4 derniers téléchargements gardé | au-dessus du bouton aligné sur la colonne (complet), à droite de la colonne aligné en bas (compact) |
| 9 | Grille = nouveau favori, hors fenêtre = nouvelle fenêtre, page = vue partagée | `SidebarDragEnd`, `SidebarTearOff`, `SplitDragMove` (bascule à la sortie de la colonne) |
| 10 | Focus : tiroirs à droite ; Ctrl+Maj+, ouvre la colonne par-dessus | à faire avec la branche Focus (§ 4) |
| 11 | Compact tout seul sous 800 DIP, sans toucher au choix | `Sidebar::Effective` ; Ctrl+Maj+, ouvre alors la colonne par-dessus |
| 12 | Textes retirés de la planche 16 | rien à faire |
| 13 | Contour de l'app partout (1 DIP blanc 10 % / 50 %) | `FocusRing` |

### Disposition de la fenêtre (24 nov.)

- Défaut : barre d'onglets en haut. Panneau latéral : réglage `layout=side-panel` (la ligne de Réglages viendra avec la
  planche du Designer). Le passage est immédiat (pas de redémarrage) : glissers, colonne par-dessus la page, tiroirs et
  popovers sont fermés, puis la fenêtre se remet en page. Le choix complet / compact du panneau est gardé à part
  (`sidebar=complete|compact`), même quand la barre du haut est utilisée.
- Ctrl+Maj+, ne fait rien avec la barre du haut (la touche va à la page, comme avant #127) et plie / déplie le panneau
  quand il est affiché. Raison : avec la barre du haut il n'y a rien à plier, et un raccourci qui changerait toute la
  disposition serait une surprise ; le seul interrupteur de disposition est la ligne de Réglages. Le raccourci n'est
  listé dans le panneau Ctrl+/ que lorsque le panneau est affiché.
- Grille des favoris (+10 / +20 / +40, comportement selon la largeur) : en attente des planches du Designer ; la
  règle actuelle (2 rangées puis « » ») reste en place d'ici là.

### Version complète de l'essai (24 nov., après-midi)

- Réglage « Position des onglets » (`tabs-position=top|side|compact`, défaut `top`) ; les anciennes clés `layout` +
  `sidebar` migrent. Le bouton de pliage, Ctrl+Maj+, et le bord du panneau changent ce même réglage (Latéral ↔ Compact) ;
  rien en barre du haut. Réglages › Onglets › Disposition (3 segments icône + nom) et dernière ligne du menu clic droit
  d'un onglet (3 icônes, info-bulle au survol et au focus clavier).
- `--tab-layout=top|side|compact` (essais, tests de Fix) : la disposition de ce lancement, jamais enregistrée ;
  `--side-panel` = `side`. Journal : « layout: top | side 248 | compact » au démarrage et à chaque changement (avec
  « (narrow window) », « (this run) »).
- Redimensionner : poignée de 6 DIP dans le bord droit de la colonne, le long de la page ; 220 … 360 (butée), sous 160
  compact d'un coup (relâcher garde le compact : réglage « Panneau compact »), au-delà de 160 réouverture à 220,
  double-clic (ou Entrée sur le bord) 248, Échap annule. Largeur mémorisée par profil (`side-width`, fichier de réglages
  du profil) ; relâcher en compact garde la largeur précédente pour la prochaine ouverture. Clavier : le bord est un arrêt
  de tabulation (← → 16, Maj 64, Début 220, Fin 360), lu « Largeur du panneau » par les lecteurs d'écran.
- Trait du bord : 2 DIP, bouts ronds, juste à l'extérieur du bord gauche de la page, suit ses coins (arc de rayon 9),
  fondu sur 12 DIP aux deux bouts ; #6B6B6B au survol, accent pendant le glisser. Les masques de coins de la page
  dessinent la partie de l'arc qui passe sur la page.
- Seuil du compact automatique : gardé à 800 (la colonne complète par défaut reste 248 ; une colonne élargie à 360
  laisse moins de place à la page, sans changer le seuil).
- Annotation en panneau (planches « Annotation · 1 à 7 ») : bandeau en haut de la zone page, page 32 plus bas, Annoter
  actif dans les outils (encre du profil, sans soulignement). Le chat est une carte à 4 DIP des bords droit et bas
  (rayon 8, filet #262626) et la page s'arrête 4 DIP avant. Propositions du Designer en constantes
  (`src/chat-host.h`) : `DrawerOverChat = true` (un tiroir s'ouvre par-dessus le chat), `ChatOverlayBelow = 900`
  (fenêtre plus étroite : le chat passe par-dessus la page). La carte « Découvrir en 1 minute » (site en ligne) s'ouvre
  à la place des popovers de la colonne (au-dessus des outils, ou à droite de la colonne compacte).

## 7. Écarts connus avec la maquette (version d'essai)

- Glisser (05) : la ligne suit la souris verticalement seulement (Paper la décale aussi de 14 DIP vers la droite) ;
  le trait d'insertion est centré dans l'espace de 2 DIP entre deux lignes (Paper le dessine 10 DIP plus haut, dans la
  ligne au-dessus).
- Le passage complet ↔ compact anime la largeur et les titres ; le contenu de la colonne change de disposition à la fin
  (pas de morphing des boutons du haut ni de la grille).
- Info-bulles : le composant de l'app (centré sous la cible, au-dessus s'il manque de place) avec un 2ᵉ ligne
  (domaine, « clic milieu pour fermer » en compact) ; Paper place celle d'un onglet compact à droite de la ligne et son
  bord est à 8 % au lieu de 6 %.
- Suggestions (06) : même composant que l'app, placé sous le champ et 430 de large ; ses couleurs restent celles de l'app.
- Glyphe du profil : celui de l'app (Phosphor Fill) au lieu du dessin simplifié de la planche.
- Planche 01 : l'icône Réglages est à 50 % et les autres outils à 70 % (copié tel quel de Paper).
- Barre compacte (02) : pas de Copier ni d'étoile dans le champ du haut (comme Paper) ; Ctrl+D reste.
- Renommer un dossier de la grille (F2) ouvre la fenêtre de modification (pas de place pour un titre dans une tuile).
- Glisser un favori pour réordonner la grille : pas encore (Ctrl+Maj+←/→ marche).
- Échap referme la colonne ouverte par-dessus la page quand la fenêtre a le focus clavier (pas quand il est dans la page) ; sinon elle se referme quand la souris la quitte, ou par
  Ctrl+Maj+, en fenêtre étroite).
- Coffre de mots de passe : la clé du champ reste à gauche de Copier, sans autre changement (hors périmètre).
- Annoter (premier outil du bas) : toujours affiché dans les versions avec le mode Annotation, dessiné comme Paper
  (marque 15 DIP blanc 70 %), grisé (`Design::Text::disabled`) là où le mode ne peut pas démarrer — page Nouvel onglet,
  page interne, fenêtre privée et, avec #125, les sites en ligne — avec l'info-bulle qui dit pourquoi ; actif : l'aspect
  de l'app. L'état vient de `AnnotationButtonDimmed()` (même règle `Ann::Available` que la barre), sans dépendre de #125.
- Rendus hors écran (`design/127-sidebar-evidence`) : les tiroirs, les info-bulles, la liste des suggestions et le texte
  tapé sont des fenêtres à part (les mêmes qu'aujourd'hui) : ils n'apparaissent pas dans les rendus du peintre de la colonne.

- Bas du trait du bord : Paper finit l'arc 2 DIP plus haut (sa page s'arrête à 734) ; le code suit la règle (arc
  concentrique au coin réel de la page, à 736).
- Chat en panneau : ses coins arrondis viennent d'une région de fenêtre (bord crénelé, pas lissé) ; dans la barre du
  haut, le chat reste comme aujourd'hui (collé au bord droit).
- Annoter actif : le fond reste l'accent à 16 % (Paper #22452D pour Travail : pas de règle trouvée pour les autres profils).
- Planches Paper avec 8 DIP de marge autour de la page : la règle de Val (4 DIP) gagne.
- Pas encore : « Gérer les favoris » sous la grille dépliée et au pied du popover compact, glisser un favori sur un
  dossier de la grille (planches du soir, à valider par Val).

## 8. Vérifications en fenêtre réelle (tournée de nuit de Fix)

Lanceurs : `build/127-sidebar-trial/Essayer le panneau lateral.cmd` (`browser.exe --side-panel`) et `Lancer (barre du haut, par defaut).cmd` (preview : profil dans ce dossier, jamais celui de Val).
1. Ouverture : colonne complète, page en carte (4 coins arrondis), boutons de fenêtre, déplacement de la fenêtre par la
   bande du haut et le haut de la colonne, double-clic = agrandir, Aero Snap sur Agrandir.
2. Ctrl+Maj+, et le bouton : pliage 180 ms, la page suit, choix gardé après redémarrage ; fenêtre < 800 : compact seul.
3. Compact : 400 ms de survol ouvrent la colonne par-dessus la page sans la déplacer ; elle se ferme en sortant ;
   la page reste cliquable à droite ; pas de coin de page visible par-dessus la colonne.
4. Onglets : clic, clic milieu, croix, menu clic droit, Ctrl+Tab / Ctrl+1…9 (la ligne revient dans la vue), 30 onglets
   (défilement + fondus), glisser pour réordonner, vers la grille (favori), vers la page (vue partagée), hors fenêtre.
5. Favoris : 0 / 5 / 12 favoris, dossier (menu), « » », Ctrl+Maj+B, réglage Jamais.
6. Tiroirs : Téléchargements (popover puis « Tout afficher »), Historique, Réglages, Ctrl+Maj+O : ancrés à droite à
   partir de y 40, bouton marqué.
7. Adresse : saisie, suggestions (430 de large sous le champ), cadenas, « Non sécurisé », puce d'autorisation, Copier,
   étoile ; en compact dans la bande du haut.
8. Mode Annotation : bandeau au-dessus de la page, page 32 plus bas ; Son des sites dans les outils.
9. Plein écran F11 : ni colonne ni bande.
