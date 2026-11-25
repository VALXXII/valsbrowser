# Fenêtre d'impression (#121) — notre fenêtre à la place de l'aperçu du moteur

Source de vérité : Paper « Browser Design » (`01M6Q7YT14ZKMGET54ASPNGQEC`), page `p-V-0`
« #121 — Fenêtres du moteur à notre design » :

- « Proposition — Impression · A (avec aperçu) » (`1LPR-0`) : la fenêtre ;
- « Proposition — Impression · états » (`1NHB-0`) : ses 10 états, validés par Val (issue #121, « maquette prête »).

Valeurs lues avec `get_jsx` (jamais sur une capture), reportées dans `src/print-dialog.h` (`Paper::`).
Exports de référence pour les comparaisons au pixel : `tests/print/paper/` (fenêtre à 1× et 1,25×, planche entière,
états 1 à 10).

## Ce qui est codé

| Élément | Planche | Code |
| --- | --- | --- |
| Panneau | 720 large, bordure 1 px `#FFFFFF14` incluse, rayon 10, `#171717`, 480 de haut dans la bordure | `Paint()` |
| Position | 59 sous le haut de la zone de page (140 − 81), centré | `Place()` |
| Voile | `#00000080` sur la zone de page ; coins bas arrondis comme ceux de la page | fenêtre à alpha par pixel (`UploadScrim`) |
| Aperçu | colonne 360, `#0F0F0F`, marge 24, écart 12, contenu centré ; feuille blanche rayon 2 (A4 portrait 280 × 396, paysage 312 × 221) | `PaintSheetMode()` : le vrai PDF du moteur, page par page |
| Pagination | chevrons 24 × 24 (glyphe 14, `#FFFFFFB3`, `#FFFFFF52` quand il n'y a pas de page de ce côté), écart 8, « Page 1 sur 3 » 14/20 `#FFFFFFB3` | `PaintSheetMode()` |
| En-tête | « Imprimer » 16/20 Medium −0,01em ; écart 4 ; « 3 feuilles » 14/20 `#FFFFFFB3` | `PaintSettings()` |
| Champs | libellé 14/20 `#FFFFFF80`, écart 8, contrôle 32 de haut rayon 8 `#FFFFFF14` | idem |
| Choix segmentés | marge 2, écart 2 ; choisi `#3E3E3E` rayon 6 ; l'autre rayon 4 `#FFFFFFB3` ; « Choisir » sans points de suspension quand il est choisi (états 1, 2) | `PaintSegments()` |
| Fonds | case 16 accent rayon 4, coche 10 ; écart 8 ; « Couleurs et images de fond » 16/20 −0,01em | `PaintSettings()` |
| Actions | en bas, écart 8 : « Annuler » (raised, `#FFFFFFCC`) et l'action (accent) ; action indisponible à 40 % (état 2) | `Kit::Button` (règles A1) |
| 1 · Choisir les pages | vignettes 120 × 170 (grille 272, colonnes 128, écart 16, rangées 12), case 18 (accent + coche 11, ou blanche bord 1,5 `#00000066`), contour accent 2 à 2, vignette non cochée à 60 %, numéro 13/16 ; champ focalisé : contour 1 px `#FFFFFF80`, curseur blanc 1 × 16 ; aide 13/18 `#FFFFFF80` « Cochez dans l'aperçu ou écrivez : 1-3, 5 » ; cases ↔ champ synchronisés | `PaintGrid()`, `Toggle()`, `FormatRanges()` |
| 2 · Plage hors du document | champ fond danger 8 %, contour danger ; icône 14 + « Le document n'a que 3 pages. » 13/18 danger ; action à 40 % ; l'aperçu redevient la feuille | `PaintRange()` |
| 3 · Imprimante en paysage | « Imprimer » ; feuille 312 × 221 centrée avec la pagination | `Layout()` |
| 4 · Aucune imprimante | liste sous le choix (376, 140, 328 de large) : `#171717`, bord `#FFFFFF14`, rayon 10, marge 6 ; ligne « Enregistrer en PDF » survolée `#333333` + coche 14 ; filet `#FFFFFF0D` ; « Aucune imprimante installée » `#FFFFFF80` ; « Ajouter une imprimante… » `#FFFFFFB3` + icône 12 (ouvre Réglages Windows › Imprimantes) | `PaintList()` (dessinée dans la fenêtre) |
| 5 · Aperçu en préparation | formes grises sur la feuille (`#ECECEC` 180 × 12 et 120 × 12, `#F2F2F2` 110, lignes `#F0F0F0`) ; « Préparation de l'aperçu… » `#FFFFFF80`, flèches masquées ; « Calcul des feuilles… » | `PaintSkeleton()` |
| 6 · Aperçu impossible | on reste dans notre fenêtre : cadre pointillé 1 px `#FFFFFF26` rayon 4, icône Jiggicons document-dismiss (verre, gris) 40, « Aperçu indisponible » 14/20 Medium `#FFFFFFE6`, texte 13/18 `#FFFFFF80` ; pas de compteur ; l'action reste | `PaintUnavailable()`, `DrawDismiss()` |
| 7 · Impression en cours | la fenêtre se ferme ; toast C8 « Impression en cours… » + nom de l'imprimante, « Annuler » | `src/print-integration.h` (`PrintJobStart`) |
| 8 · Erreur d'impression | toast C8 « Impression impossible » · « L'imprimante est hors ligne », « Réessayer », ✕ ; reste affiché jusqu'à une action | `PrintJobEnded` |
| 9 · Survol et focus | liste : survol `#FFFFFF1F`, focus contour 1 px `#FFFFFF80` ; segment : survol `#FFFFFF0A` + texte blanc, focus contour du choix ; case : rangée `#FFFFFF0A` rayon 6 débordant de 6, focus contour ; flèches : survol `#FFFFFF14` + glyphe blanc, focus contour ; boutons : règles A1 | `OuterRing()` et peintres |
| 10 · Fenêtre étroite | zone de page sous 760 : 400 de large, une colonne ; en-tête « Imprimer », « 3 feuilles », vignette « Aperçu » (`#FFFFFF0A` rayon 8, feuille 32 × 44) ; 434 de haut | `Layout()`, `PaintChip()` |

Rendus hors écran comparés aux exports Paper (`tests/print/run.py`), pixels qui diffèrent de plus de 40
(lissage du texte) : fenêtre 0,18 % (1×) et 0,70 % (1,25×) ; états 1 : 0,60 % ; 2 : 0,36 % ; 3 : 0,27 % ; 4 : 0,25 % ;
5 : 0,18 % ; 6 : 0,22 % (1× et 1,25×) ; 10 : 0,34 %. L'export Paper est décalé d'environ un demi-pixel et gère la couleur
de l'accent (`#4766F6` dans le PNG pour le jeton `#3E67FF`, que le code garde).

## Écarts avec la planche (impossibles exactement, ou incohérents dans Paper)

1. **Toast « en cours »** : la planche affiche « HP LaserJet Pro M404 · page 2 sur 3 ». Le moteur (WebView2 `Print`)
   ne donne aucune progression : le toast affiche le nom de l'imprimante seul, et l'arc du cercle est figé à celui de
   la planche (116,6°).
2. **« Annuler » pendant l'impression** : l'impression du moteur ne peut pas être arrêtée ; « Annuler » supprime le
   travail de la file d'attente Windows de l'imprimante dès qu'il y arrive (même imprimante, même nom de document,
   envoyé depuis « Imprimer », par l'utilisateur). À vérifier sur une vraie imprimante.
3. **Raison de l'erreur** : seul « L'imprimante est hors ligne » est dessiné (statut « imprimante indisponible » du
   moteur) ; pour les autres échecs, le toast n'a pas de deuxième ligne.
4. **Hauteur des planches d'états** : les états 1, 3, 4, 5, 6 font 480 de haut bordure comprise, la fenêtre et l'état
   2 font 482 ; le code garde 482 (contenu centré : 1 px d'écart sur l'aperçu paysage).

## Ce que les planches ne montrent pas (à dessiner par le bot Designer)

Codé au plus près des règles des planches, en attendant une maquette :

1. **Document long en vignettes** (état 1 avec plus de 4 pages) : la grille commence en haut et défile à la molette,
   sans barre de défilement.
2. **Focus clavier d'une vignette** (Tab sur la grille, flèches, Espace coche) : contour 1 px `#FFFFFF80` autour de
   la vignette (règle de l'état 9).
3. **Vignettes en paysage** : 120 de large, hauteur selon le papier (A4 : 85).
4. **Liste des imprimantes avec des imprimantes installées** : « Enregistrer en PDF », filet, puis une ligne par
   imprimante (style des lignes de l'état 4, coche sur la choisie) ; pas de « Ajouter une imprimante… ». Liste trop
   longue : elle défile dans la fenêtre.
5. **Plage mal écrite** (« 2-x ») : champ en erreur, sans message (seul « Le document n'a que N pages. » est dessiné) ;
   la ligne d'aide reste.
6. **Aperçu impossible en fenêtre étroite** : la vignette « Aperçu » montre un cadre fin à la place de la feuille.
7. **La vignette « Aperçu »** (état 10) n'est pas cliquable (aucune action dessinée).
8. **Échec de « Enregistrer en PDF »** : rien n'est affiché, seulement le journal.

## Comportement

- Ouvrent notre fenêtre : Ctrl+P (y compris quand la page a le focus), « Imprimer… » du menu de l'onglet, « Imprimer »
  du menu de la page (l'entrée du moteur est remplacée), `window.print()` du document principal d'une page.
- Première destination : « Enregistrer en PDF » (comme dessiné) ; la dernière choisie est gardée pour la session.
- Papier : celui de la région Windows pour le PDF (A4 en France) ; pour une imprimante, son papier par défaut
  (lu hors du fil de l'interface).
- Fonds cochés par défaut (comme dessiné). En-têtes et pieds de page du moteur : non (pas sur la planche).
- « Enregistrer » : le PDF exactement montré (mêmes réglages, plages comprises) va dans le dossier de
  téléchargements et apparaît dans la liste des téléchargements, comme « Enregistrer en PDF » du menu.
- « Imprimer » : `ICoreWebView2_16::Print` avec l'imprimante, l'orientation, les fonds et les plages ; le papier est
  celui de l'imprimante ; toasts C8 (états 7 et 8), « Réessayer » relance le même travail.
- Échap = Annuler (ou ferme la liste) ; Entrée = le contrôle focalisé (l'action par défaut) ; Tab / Maj+Tab ; ← → sur
  les choix et la pagination ; ↑ ↓ Alt+↓ F4 ouvrent la liste des imprimantes ; Page préc. / suiv. tournent les pages
  (ou font défiler la grille).
- Changer d'onglet ou de profil, ou fermer l'onglet, annule la fenêtre.
- Une page qui rappelle `window.print()` dans la seconde qui suit une annulation est ignorée (boucles).

## Limites (WebView2 / Windows)

- `window.print()` d'un **cadre** (iframe) garde l'aperçu du moteur : les API d'impression de WebView2 n'impriment
  que le document principal.
- `window.print()` n'est plus bloquant : une page qui fait `window.print(); window.close()` se ferme aussitôt.
- Le bouton « Imprimer » de la barre du **lecteur PDF** du moteur reste celui du moteur (Ctrl+P y ouvre la nôtre).
- Copie publique : l'icône de l'état 6 (Jiggicons document-dismiss, payante) y est remplacée par Phosphor file-x (MIT),
  et les exports Paper de l'état 6 par leur version sans icône (`tools/public-mirror/paid-icons`).
- Une seule opération d'impression à la fois par page (règle du SDK) : les demandes sont mises en file.
- Aperçu : Windows.Data.Pdf dessine les pages sur un périphérique Direct3D logiciel (WARP) créé et libéré par le fil
  de l'aperçu ; son moteur de rendu « par flux » laisse un périphérique dans une variable statique qui, libéré à la
  sortie du processus, termine celui-ci avec le code 0x87A (vu en test) : il n'est utilisé qu'en secours.

## Tests

- Sans écran (jour) : `python -I tests/print/run.py` — plages de pages, logique de la fenêtre et de ses états sur des
  fenêtres jamais affichées, rendus hors écran de la fenêtre et des états 1 à 6 et 10 comparés au pixel avec Paper,
  aperçu sur des PDF générés (300 pages), l'arbre d'accessibilité (une racine, des clés uniques, sans boucle), la
  boucle d'animation du navigateur lui-même (`src/main.cpp`) qui fait entrer et sortir la fenêtre quand rien d'autre
  ne bouge, et le vrai moteur dans un hôte WebView2 jamais affiché.
- Fenêtre réelle (nuit seulement) : `python tests/print/print_qa.py --app <build neuf> --out <dossier>` — trois
  sessions (imprimantes factices ; échec « hors ligne » ; sans imprimante ni aperçu), toasts compris ; imprimantes
  factices et « puits » d'impression sous `--workspace-test` : aucune vraie imprimante, Réglages Windows jamais ouverts.
- Nuits du 24 et du 25/11 : la fenêtre restait à ~15 % puis à 0 % d'opacité. La boucle d'animation du navigateur
  sortait (« rien ne bouge ») avant de lire le besoin d'images de la fenêtre d'impression : son fondu n'en recevait
  aucune (ou seulement celles d'une autre animation en cours). Même nuit : « Page précédente » portait la clé UIA du
  groupe de la fenêtre, l'arbre bouclait (un client UIA y lisait ~1000 nœuds en 9 s).
