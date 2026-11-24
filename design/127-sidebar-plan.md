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
