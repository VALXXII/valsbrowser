# Vue partagée — harmonisation du flow Paper (8 novembre)

Page modifiée : *Browser Design*, **Split View — flow**,
https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-3-0

Objectif : que les planches 00 à 23 racontent une seule implémentation, celle de
[split-view.md](split-view.md) (décisions 6 à 9, 14, 15 et « Lot retouches
utilisateur »). Aucune planche supprimée, aucune étape ajoutée. Le code, le README et
`split-view.md` ne sont pas touchés.

## Règles appliquées partout

| Sujet | Valeur |
| --- | --- |
| Gap entre sites | 4 DIP (`--spacing-browser-4`), dans les deux orientations |
| Groupe dans la barre | bordure 1 DIP blanc 10 %, coins extérieurs 6 / intérieurs 2, séparateur 2 × 10 blanc 30 % (`#FFFFFF4D`), actif `#292929`, autres membres `#222222` ; onglet libre sans bordure ni surface. De 2 à 7 membres. |
| Panneau actif | `0 0 0 1px #171717, 0 0 0 3px` blanc 10 % (souris) ; blanc 50 % au clavier. Plus aucun contour accent. |
| Poignée | repos : rien (gap neutre) ; survol : 8 × 36 `#2F2F2F` arrondie (36 × 8 empilé) ; glisser : poignée et ligne `#3E67FF` ; clavier : anneau blanc 50 %. |
| Resize en cours | pages touchées couvertes de `#171717`, favicon 36, nom court (16 px, 500) puis domaine nu (14 px), blanc 50 % plein (5,26:1) ; vrais sites au relâchement après redraw. |
| Accent `#3E67FF` | aperçu de dépôt (voile 16 %, bord 2 DIP, pastille) et resize actif seulement. |
| Nom court | pastille et vignette : « Mobbin », « YouTube », jamais le titre HTML. |
| Menu | bloc V2 (p-7-0, V2-1) ; V1, V3 et croix non injectés. |

## Planches touchées

- **00** : sous-titre (aligné sur l'implémentation), bande 40–84 « reste dans la
  barre », formule de capacité au gap 4 (3 / 7 / 9 / 14), lignes Actif et Couleur
  réécrites, trois lignes ajoutées (Groupe, Redimensionner, Menu d'onglet),
  explorations hors flow signalées (menus V1/V3, archive dépôt A/B/C). Déplacée à
  gauche de 01 (x −1 380) : agrandie, elle chevauchait la planche 04.
- **03** : bande 40–84 conforme à l'écart assumé (pas de rubber band vertical).
- **07** : groupe bordé, gap 4, contour neutre 10 %, poignée au repos retirée, repères
  et spec réécrits.
- **08** (référence utilisateur) : conservée. Seuls changements : la poignée est
  masquée (calque gardé, renommé « survol seulement ») car le pointeur est dans la
  page ; libellé de l'actif à 100 % et membre inactif en `#222222` (décision 8).
- **09** (retouche utilisateur, état resize) : conservée ; poignée passée de 6 × 48 à
  8 × 36 accent centrée sur la ligne, gap 4, « YouTube », repère « gap 4 », spec
  réécrite (repos / survol / glisser / relâcher, clavier).
- **10** : réécrite dans l'état resize (pages couvertes, ligne et poignée accent) avec
  arrêt net à 360 ; rubber band et voile « compressé » retirés ; 732 DIP au lieu de 736.
- **11, 18** : groupe bordé, gap 4, aperçus recalés (584 / 388 DIP), pastille et
  vignette « Mobbin », emplacement d'origine recalé, chemins clavier en V2.
- **12a** : ancien bloc « Placer à droite de Youtube » remplacé par un clone du menu V2
  (en-tête « Avec YouTube · affiché », quatre positions, « Avec un autre site › »),
  liseré de cible sur l'onglet YouTube, spec réécrite.
- **12b** : groupe bordé, contour neutre, menu à 280 de large avec
  « Avec un autre site › » sous les commandes du groupe ; renommée « gérer le groupe ».
- **13** : panneau sortant qui glisse retiré (bounds changés une fois) ; spec réécrite.
- **15** : motion (resize opaque, arrêt net, inversion sans glissement), clavier V2,
  contrastes (contour clavier 5,26:1, poignée accent au glisser), UIA « YouTube ».
- **17** : groupe bordé, gap 4, poignée horizontale de survol 36 × 8 `#2F2F2F`,
  contour neutre, 516 DIP.
- **19** : groupe de 3 bordé (premier / milieu / dernier), calcul 4 × 360 + 3 × 4 =
  1 452 > 1 172, raison du menu V2.
- **20** : groupe de 4, gap 4, 635 DIP par site, contour neutre.
- **21** : 7 sites de 361 DIP (Le Monde.fr ajouté), groupe de 7, formule au gap 4.
- **22** : groupe de 4 bordé, gap 4, contour neutre, repli sans glissement.
- **23** : séparateur (gap 4, poignée 8 × 36), tableau de capacité au gap 4 (732 → 2,
  1 180 → 3 × 388, 1 460 → 4, 1 920 → 5 × 379, 2 560 → 7 × 361, 3 440 → 9 × 378,
  5 120 → 14 × 361), arrêt net au minimum.

Non modifiées (déjà cohérentes, avant split) : 01, 02, 04, 05, 06, 14, 16 (texte du
menu seulement).

## Décisions confirmées par le code (preview `build/split-dev-5`)

1. **« Avec un autre site › » sans retrait vide** : aligné sur le texte du menu.
   Seules les lignes de position gardent glyphe et retrait de 28 DIP. « Insérer entre
   › » et « Remplacer un site › » gardent leur retrait, alignées sur « Ajouter au
   début / à la fin ».
2. **12b** : un seul bloc « Inverser, Empiler / Côte à côte, Redimensionner, Quitter,
   Avec un autre site › ».
3. **Coins bas** : rayon 6 sur les coins extérieurs bas (page seule, panneaux
   extrêmes côte à côte, panneau du bas empilé), par masques natifs antialiasés. Le
   contour actif suit l'arc (rayon 8 sur sa ligne centrale). Coins hauts droits.
4. **État resize** : #171717 opaque, favicon 36 (rayon 4), 12 DIP, nom court 16
   medium, 4 DIP, domaine nu 14 regular ; nom et domaine en blanc 50 % plein
   (5,26:1). Les planches 09 et 10 ont été réalignées : elles affichaient
   #FFFFFF80 × 0,9, soit 4,48:1, sous le seuil. Ellipse si trop long, pas de domaine
   pour file: et about:. Ligne accent de 2 DIP et poignée accent 8 × 36 sur le seul
   séparateur actif, pas de contour actif pendant l'état. Déclenchement à 3 DIP de
   glisser, fondu de 120 ms une fois les pages rendues.

## Limites

- Les titres d'onglet gardent « Youtube » (titre HTML de la maquette) ; seuls le nom
  court, les pastilles, les vignettes, les états resize et les annonces UIA disent
  « YouTube ».
- Pas de vérification sur rendu natif : QA par captures Paper uniquement.
- Valeurs recoupées avec le premier agent après coup (voir « Décisions confirmées »).
- Page des variantes (p-7-0) et archive (p-6-0) non modifiées.
