# Vue partagée (split view)

État : **implémentée et vérifiée nativement** (voir « Implémentation native »). Le
flow Paper validé (planches 00 à 23) reste la référence ; les écarts assumés sont
listés plus bas.

Paper : fichier *Browser Design*, page **Split View — flow** (page dédiée, aucun
écran existant modifié) :
https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-3-0

Les écrans clonent le chrome du prototype **Lisière — prototype actuel V3** (Page 1)
et suivent la géométrie native de `src/design-system.h`. Le pas d'onglet utilisé est
le pas natif (128 + gap 6), pas l'ancien gap de 12 encore dessiné dans V3. Le contenu
est inséré de 4 DIP (`main.cpp`, `Layout()` : bounds `{4, 84, w-4, h-4}`).

## Artboards

Grille de lecture, quatre par ligne. Chaque écran est une fenêtre 1180 × 815 avec,
dessous, une bande de spec en trois colonnes : geste, réponse et motion, clavier et
accessibilité. Les annotations magenta (`#D4148C`) forment une couche de spec : elles
ne font jamais partie de l'interface. Elles étaient bleues (`#2F6BFF`) dans la première
version et ont changé de couleur à l'arrivée de l'accent : à 4° de sa teinte, elles se
seraient confondues avec lui.

| # | Artboard | Contenu |
| --- | --- | --- |
| 00 | Vue d'ensemble | Machine d'états, décisions |
| 01 | État initial | Trois onglets, Youtube actif |
| 02 | Pression | Surface #292929 dès la pression, seuil de 4 px |
| 03 | Réordonner ou soulever | Bandes y ≤ 40 / 40–84 / ≥ 84, hystérésis 64 |
| 04 | Soulever l'onglet | Onglet 1:1, emplacement d'origine, aperçu en vol |
| 05 | Zone droite | Voile accent au rect exact, hystérésis ± 24 au milieu |
| 06 | Zone gauche | Re-ciblage depuis la valeur affichée |
| 07 | Vue partagée établie | Paire dans la barre, panneau actif, adresse |
| 08 | Panneau actif | Le chrome suit le panneau actif |
| 09 | Redimensionner | Cible 16 DIP, poignée 6 × 48, aimants ⅓ ½ ⅔ |
| 10 | Taille minimale | 320 DIP, rubber band, fenêtre étroite |
| 11 | Remplacer ou inverser | Dépôt sur un panneau existant |
| 12a | Sans glisser : créer | Menu d'onglet natif + « Placer à droite/gauche de … » |
| 12b | Sans glisser : gérer | « Inverser les panneaux », « Quitter la vue partagée » |
| 13 | Quitter | Retour dans la barre, la paire se défait |
| 14 | Annuler | Échap / hors fenêtre / Alt+Tab, même chemin |
| 15 | Motion, clavier, accessibilité | Tables de ressorts, clavier, contrastes, UIA |
| 16 | Quatre zones sur une page seule | Diagonales : gauche/droite côte à côte, haut/bas empilé |
| 17 | Empilés | Séparateur horizontal, hauteur minimale 256 px |
| 18 | Insérer un troisième site | Centre = remplacer, quart extérieur = insérer, rect final |
| 19 | Refus : pas assez de place | Pastille neutre « Pas de place pour un 4ᵉ site » |
| 20 | Quatre côte à côte | Fenêtre 2 560 × 1 080 DIP, 4 × 632 DIP |
| 21 | Au-delà de quatre | 2 560 DIP : 6 × 419 DIP, sans plafond arbitraire |
| 22 | La fenêtre rétrécit | Le moins récent se replie en rail de 32 DIP, rien ne se ferme |
| 23 | Règles de disposition | Unités, minimums, capacité, comportements |

## Décisions

1. **Intention par la hauteur du pointeur.** y ≤ 40 (bas de `TabStrip::regionBottom`) :
   réordonnancement actuel, inchangé. 40–84 : tension, l'onglet suit verticalement
   avec résistance (rubber band 0,55). y ≥ 84 (haut du contenu) : l'onglet se soulève.
   On ne le rattache à la barre qu'au-dessus de 64, soit 20 DIP d'hystérésis. Il n'y a
   ni mode ni modificateur : un seul geste.
2. **Zones = bord le plus proche.** Sur une page seule, les diagonales du contenu
   découpent quatre triangles : gauche/droite placent côte à côte, haut/bas empilent
   (étape 16). Pour changer de zone, il faut dépasser la diagonale de 24 DIP. Les
   « moitiés » de la première version sont devenues les triangles gauche et droit.
   Relâcher dans la bande de tension réordonne. Relâcher hors de la fenêtre annule.
   On ne crée pas de fenêtre par arrachement.
3. **Partenaire.** C'est l'onglet actif. Si l'onglet glissé est lui-même l'actif, le
   partenaire est le dernier onglet consulté (MRU). Avec un seul onglet, rien ne se
   soulève.
4. **Aperçu = résultat.** Un voile accent à 16 % (`--color-browser-split-tint`), bordé
   d'un trait accent de 2 DIP intérieur, rayon 6, occupe le rect exact du futur panneau.
   La page reste lisible dessous. Une pastille accent pleine, en texte blanc, porte
   **seulement le nom de l'onglet déplacé** (« YouTube »). La position et le partenaire
   ne sont plus écrits dans la pastille (retour utilisateur) : le voile les montre, et
   ils restent annoncés aux lecteurs d'écran. Le refus garde sa pastille explicite.
   La page dessous n'est
   **pas** redimensionnée pendant le glisser, pour éviter un relayout WebView à chaque
   changement de zone. La plaque grise opaque (canvas 88 %) de la première version a
   été remplacée à la demande : elle masquait la page.
5. **Dépôt.** L'onglet déposé devient le panneau actif. La paire prend la place du
   partenaire dans la barre (Youtube, Paper, puis Luma). Le voile s'efface en 120 ms
   après le premier frame de la page, sans flash blanc. Son bord devient le contour
   actif : même couleur, même endroit, aucune rupture.
6. **Disposition.** Gap entre panneaux = 4 DIP (`--spacing-browser-4`), d'après la
   retouche de l'utilisateur sur la planche 08. Historique : 6 DIP, puis 8. La poignée
   ne vit plus dans le gap (voir 9), qui redevient une simple ligne neutre.
   Insets de fenêtre inchangés (4). Chaque coin extérieur bas garde un rayon de 6,
   concentrique au rayon 10 de la fenêtre.
7. **Panneau actif.** Le contour est neutre et suit la retouche de la planche 08 : 1 DIP
   de canvas, puis 2 DIP de blanc. Il dépend de la modalité d'entrée :
   - activé à la souris (clic, focus pris bouton enfoncé), blanc 10 %, discret ;
   - activé au clavier (Tab dans la page, F6, menu), blanc 50 %, 5,26:1, le même
     anneau que le focus clavier du chrome.
   Le survol n'active jamais. L'accent #3E67FF ne sert plus de contour permanent : il est
   réservé à l'aperçu de dépôt et à la manipulation. Indices redondants : l'adresse et
   la surface de l'onglet actif. Historique : contour accent 2 DIP (3,92:1), remplacé à
   la demande de l'utilisateur.
8. **Groupe dans la barre** (retouche utilisateur, planche 08). Chaque membre a une
   bordure neutre de 1 DIP en blanc 10 % (`--opacity-browser-focus`). Coins extérieurs
   6, intérieurs 2. Entre deux membres, un séparateur de 2 × 10 DIP en blanc 30 %.
   L'actif se lit par sa surface (#292929, texte 100 %). Dans le groupe affiché, les
   autres membres sont en #222222 ; un groupe hors écran garde ses bordures, sans
   surface. Cela vaut pour 3 membres ou plus : premier, milieux (2/2) et dernier. Un
   onglet libre n'a ni bordure ni surface au repos.
9. **Redimensionnement.** Le séparateur suit 1:1. Au repos, seul le gap neutre de
   4 DIP est visible. La poignée (Paper `66A-0` « Poignée (repos) ») mesure 8 × 36 DIP,
   en #2F2F2F, entièrement arrondie. Elle n'apparaît qu'au survol, au glisser ou au
   focus clavier, et déborde de 2 DIP sur chaque page. Ni accent ni blanc au repos.
   La cible au pointeur fait toujours 16 DIP (gap de 4 + 6 DIP sur chaque page, voir
   « Grips »), indépendante du dessin. Aimants ⅓, ½ et ⅔, choisis sur la position
   projetée (décélération 0,998). Double-clic : répartition égale.
10. **Minimums.** Un site fait au moins 360 px CSS de large et 256 px CSS de haut
    (détail plus bas). La première version disait 320 DIP ; c'est remplacé. Sous la
    place nécessaire, rien ne se ferme : les sites se replient en rails (étape 22).
11. **Remplacer, inverser, quitter.** Déposer un onglet sur un panneau le remplace ;
    l'onglet remplacé redevient simple, juste après la paire. Déposer une moitié de la
    paire sur l'autre panneau inverse les panneaux. Ramener une moitié au-dessus de 64,
    utiliser le menu, Ctrl+W ou fermer un des deux onglets quitte la vue partagée.
12. **Motion.** Ressorts critiques partout (amortissement 1), toujours repris depuis la
    valeur affichée, avec la vitesse du pointeur au relâchement. Aucun rebond : chaque
    cible est un emplacement aimanté, jamais un lancer libre. Le détail est en 15.
13. **Mouvement réduit** (`Motion::PreferredReducedMotion()`). La manipulation directe
    reste 1:1. Glissements et ressorts deviennent des fondus de 120 ms. Le séparateur
    saute à l'aimant et l'arrêt au minimum est net.
14. **Couleur d'accent, périmètre.** `#3E67FF` est réservé à l'aperçu de dépôt et à la
    manipulation (voile, bord, pastille). Le contour actif et la poignée sont
    désormais neutres (décisions 7 et 9). Deux
    tokens Paper portent ce rôle : `--color-browser-split-accent` et
    `--color-browser-split-tint` (16 %). La barre d'onglets, la paire comprise, garde
    les gris de sélection du navigateur. Le panneau qui sort (étape 13) est une page qui
    s'efface, pas une cible, donc sans accent. Rien d'autre dans le navigateur n'adopte
    cette couleur.
15. **Sans glisser (WCAG 2.5.7).** Le menu d'onglet (clic droit, touche Menu, Maj+F10
    sur l'onglet focalisé) porte le bloc V2 retenu par l'utilisateur ; voir « Menu
    d'onglet V2 » plus bas. Le menu d'un membre du groupe affiché garde « Inverser les
    panneaux », « Empiler les panneaux » ou « Placer côte à côte », « Redimensionner les
    panneaux » et « Quitter la vue partagée ». F6 parcourt les panneaux dans l'ordre, puis la
    barre ; Maj+F6 en sens inverse. Les séparateurs ne sont plus des arrêts F6, car
    avec six sites le cycle deviendrait trop long. « Redimensionner les panneaux » donne
    le clavier au séparateur voisin : ← → (↑ ↓ si empilés) par 16 DIP, Maj par 64,
    Tab pour le séparateur suivant, Début/Fin, Entrée pour l'égalité, Échap. Aucun nouveau
    raccourci global : Ctrl+Alt+flèches est AltGr sur un clavier AZERTY et sert à la
    rotation d'écran sur certains pilotes.

## Orientations et nombre de sites

**Deux orientations, sans ambiguïté.** *Côte à côte* : les panneaux sont alignés en
ligne, séparés par des séparateurs **verticaux** (étapes 07 à 11, 18 à 22). *Empilé* :
les panneaux sont superposés en colonne, séparés par des séparateurs **horizontaux**
(étapes 16, 17). Un groupe n'a qu'une orientation en v1. Sur un groupe côte à côte, le
haut et le bas d'un panneau remplacent : on ne mélange pas les orientations.
« Empiler les panneaux » et « Placer côte à côte » basculent le groupe quand la place
le permet.

**Minimums et unités.** Le projet n'a pas de breakpoint téléphone :
`--breakpoint-browser-min` (560) et `WM_GETMINMAXINFO` (560 × 300 DIP) fixent la plus
petite *fenêtre*, pas la largeur d'un site. Proposition :

- **Largeur minimale d'un site : 360 px CSS.** C'est la largeur de téléphone courante
  la plus étroite, et elle reste au-dessus du plancher de reflow WCAG 1.4.10 (320).
- **Hauteur minimale d'un site empilé : 256 px CSS**, la hauteur de reflow de WCAG
  1.4.10.
- **px CSS (ce que voit le site) = DIP / zoom de page.** Le zoom est unique dans ce
  navigateur (`settings.zoomPercent` → `put_ZoomFactor`). Le minimum en DIP vaut donc
  360 × zoom : 450 DIP à 125 %.
- **Pixels physiques = DIP × DPI / 96.** Ils changent la netteté, pas la largeur
  responsive vue par le site. Un écran 3 440 px à 150 % donne 2 293 DIP, soit 6 sites.
- On garantit seulement ≥ 360 px CSS. Les breakpoints des sites tiers sont inconnus,
  et la mise en page de chaque site à cette largeur lui appartient.

**Capacité, sans plafond arbitraire.** Côte à côte :
`n max = ⌊(contenu + 4) / (360 × zoom + 4)⌋`, avec contenu = largeur de fenêtre − 8.
Empilé : `⌊(hauteur de contenu + 4) / (256 × zoom + 4)⌋`, avec hauteur de contenu =
hauteur de fenêtre − 89.

| Fenêtre (DIP, zoom 100 %) | Côte à côte | Largeur par site |
| --- | --- | --- |
| 732 | 2 | 360 |
| 1 180 (référence) | 3 | 388 |
| 1 460 | 4 | 360 |
| 1 920 | 5 | 379 |
| 2 560 | 7 | 361 |
| 3 440 | 9 | 378 |
| 5 120 | 14 | 361 |

(Gap de 4 DIP depuis la retouche de la planche 08 ; avec le gap de 8, on avait
2 sites dès 736, 6 à 2 560 et 13 à 5 120.)

En hauteur : 815 DIP donne 2 sites empilés, 1 080 DIP en donne 3.

**Zones sur un groupe existant.** Le centre d'un panneau (moitié centrale) remplace ce
site. Le quart extérieur de chaque panneau insère à la frontière voisine ; les quarts
de part et d'autre d'une frontière visent la même insertion. L'aperçu montre le rect
final : le nouveau site reçoit 1 / (n + 1) du contenu, et les autres gardent leurs
proportions entre eux, chacun au-dessus du minimum (étape 18).

**Refus explicite.** Si n + 1 sites passeraient sous le minimum, il n'y a pas de zone
d'insertion. Une pastille neutre (#292929, icône ⊘, texte blanc) suit le pointeur :
« Pas de place pour un 4ᵉ site · 360 px minimum chacun ». Son détail en blanc 60 %
mesure 6,22:1. Il n'y a ni voile ni accent : l'accent ne marque que les cibles valides.
Relâcher sur un refus annule. Le remplacement au centre des panneaux reste possible.
Dans le menu, la commande correspondante est indisponible, avec la même raison lue par
UIA (étape 19).

**Redimensionnement à n sites.** Un séparateur ne déplace que ses deux voisins, bornés
à leur minimum avec rubber band. À deux sites, les aimants sont ⅓, ½ et ⅔ ; au-delà,
seules comptent les positions de répartition égale. Double-clic sur un séparateur :
répartition égale de tout le groupe.

**La fenêtre rétrécit sans perte.** Tous les sites rétrécissent ensemble jusqu'au
minimum. Ensuite, le moins récemment utilisé se replie en rail de 32 DIP
(`Size::tabMin`, favicon), et l'actif n'est jamais replié. Cliquer un rail fait revenir
ce site et replie à sa place le moins récent des visibles. Agrandir la fenêtre rouvre
les rails dans l'ordre inverse, avec leurs proportions. La WebView d'un rail reste
vivante (masquée, éventuellement en veille) : aucun site n'est fermé ni rechargé
(étape 22).

**Fermer et retour des onglets.** Ctrl+W ferme le site actif : ses voisins se
partagent sa place, et l'actif devient le voisin de droite, ou de gauche s'il était
dernier. Avec un seul site restant, la vue partagée se ferme. Ctrl+Maj+T rouvre le
site fermé comme onglet simple. Sortir un onglet du groupe dans la barre le rend
indépendant, de la même façon.

**Barre d'onglets.** Le groupe forme une suite d'onglets bordés et séparés (décision 8),
dont l'actif est en #292929. Les onglets rétrécissent comme tous les onglets
(128 → 32 DIP).

**Grille 2 × 2, examinée et pas retenue en v1.** Elle tient géométriquement à 1 180 ×
815 (583 × 360 par site), mais elle demande un arbre de disposition à deux niveaux,
huit cibles de dépôt par panneau et un ordre clavier en deux dimensions. Elle ne
remplace pas « quatre côte à côte ». Elle pourra devenir une troisième orientation plus
tard, sur le même modèle.

**Mouvement réduit.** Insertion, repli, retour de rail et refus deviennent des fondus de
120 ms. Le glisser reste 1:1.

## Contrastes mesurés

Calcul sRGB WCAG 2.2 sur les paires rendues, voile composé sur le pire fond :

| Élément | Paire | Ratio |
| --- | --- | --- |
| Texte de la pastille, blanc | accent #3E67FF plein | 4,58:1, passe 1.4.3 (4,5:1) |
| Bord d'aperçu, accent 2 DIP | page blanche · page noire | 4,58:1 · 4,59:1 |
| Bord d'aperçu sur la page voilée | blanc voilé 16 % | 3,71:1 |
| Texte de page sous le voile 16 % | #171717 sur blanc voilé | 14,52:1 |
| Contour actif au clavier, blanc 50 % | canvas #171717 | 5,26:1, passe 1.4.11 |
| Contour actif après un clic, blanc 10 % | canvas #171717 | 1,32:1, voulu discret (l'adresse et la surface de l'onglet portent l'état) |
| Poignée #2F2F2F (survol) | page blanche · canvas | 13,4:1 · 1,33:1 (page sombre : curseur ↔ et anneau clavier en relais) |
| Menu V2 : en-tête #FFFFFF99 · raison #FFFFFFB3 | menu #171717 | 7,0:1 · 9,1:1 |
| Menu V2 : glyphe 50 % · second mot 50 % · « · affiché » 50 % | menu #171717 | 5,2:1 · 5,26:1 · 5,26:1 (relevés de 45 % et 35 %, qui donnaient 4,48:1 et 3,2:1, avec l'accord du designer) |
| Pastille à 50 % de blanc (ancienne plaque) | plaque 88 % sur page blanche | 4,44:1, échouait 1.4.3 |
| Détail de la pastille de refus, blanc 60 % | #292929 | 6,22:1 |
| Emplacement d'origine, liseré 10 % | canvas | 1,32:1, décoratif |
| Moitié inactive #222222 | canvas | 1,13:1, regroupement seul |

## Implémentation native

Le flow validé (planches 00 à 23) est implémenté dans le navigateur natif. La vue
partagée affiche de vraies WebView2 interactives, chacune dans son propre contrôleur ;
aucune page n'est dupliquée ni rechargée par la mise en page.

### Fichiers

| Fichier | Rôle |
| --- | --- |
| `src/split-view.h` | Modèle pur, sans Win32 : unités, capacité, disposition et rails, zones (diagonales, centre/quarts, hystérésis, refus), insertion 1/(n+1), redimensionnement borné, aimants, ressort critique avec vitesse |
| `src/split-overlay.h` | Fenêtre de surimpression : onglet soulevé, aperçu accent, pastille de refus |
| `src/main.cpp`, section « Split view » | Groupes, `ApplyPages()` (seul endroit qui place et montre les contrôleurs), glisser/soulever/déposer, annulation, séparateurs, clavier, menu, session, annonces |
| `src/chrome.h` | Onglets du groupe joints, emplacement de l'onglet soulevé, contour actif, rails, poignées |
| `src/session.h` | Format `browser-session-2` (groupes, orientation, poids). Le format 1 est toujours relu, et des groupes corrompus ne coûtent jamais les onglets |
| `tests/split-view/split_view_test.cpp` | Tests du modèle |
| `tests/ui/split_ui_test.cpp` | Tests du comportement par la vraie `WndProc` (sans WebView2) |
| `tests/session_test.cpp` | Tests des formats 2 et 1, et des groupes corrompus |
| `test-split-view.ps1` | Lance les deux fixtures ; `-Native` lance aussi le test réel |

### Choix d'architecture

- **Un groupe = des onglets.** L'ordre des panneaux suit l'ordre des onglets, et le
  panneau actif est l'onglet sélectionné. L'adresse, Retour/Suivant, Actualiser, Copier
  le lien, Ctrl+W et les raccourcis suivent donc le panneau actif sans routage
  supplémentaire. Un groupe de moins de deux onglets se dissout de lui-même.
- **`ApplyPages()`** remplace la boucle de visibilité de `Select` et celle des bounds
  de `Layout`.
  - Un onglet seul garde les règles existantes : accueil masqué, contrôleur masqué
    jusqu'à sa première navigation.
  - Un groupe visible montre chaque membre non replié à sa place.
  - Les bounds et la visibilité inchangés ne sont pas renvoyés au contrôleur.
  - Un site affiché n'est jamais mis en veille, et un site qui réapparaît est réveillé.
- **Focus dans une page = panneau actif.** C'est l'événement `GotFocus` du contrôleur.
- **Surimpression.** Une popup en couches, sans activation et transparente à la
  souris, appartient à la fenêtre principale. Elle n'existe à l'écran que pendant un
  glisser, et sa surface est libérée au relâchement. Ses ressorts avancent sur le
  compositor clock existant : aucun timer au repos.
- **Accent `#3E67FF`.** Il ne sert qu'au voile et au bord d'aperçu, au contour actif et
  à la poignée. Un seul accent est visible à la fois : le contour s'efface pendant un
  aperçu. Le refus est une pastille neutre.

### Conversion px CSS ↔ DIP, validée sur de vraies pages

Les bounds des contrôleurs sont posés en pixels physiques : `S(dip) = dip × DPI / 96`.
WebView2 suit l'échelle du moniteur, donc un site voit des px CSS = DIP / zoom de
page, et `devicePixelRatio` = DPI / 96 × zoom. Les minimums (360 × 256 px CSS) sont
convertis avec le zoom unique du navigateur (`settings.zoomPercent`), puis recalculés
quand il change. Mesures réelles à DPI 120 (Windows à 125 %), relevées par
`ExecuteScript` dans chaque page :

| Cas | Panneau (DIP) | innerWidth × innerHeight | devicePixelRatio |
| --- | --- | --- | --- |
| Deux sites côte à côte, zoom 100 % | 583,2 | 584 × 728 | 1,25 |
| Trois sites | 387,2 / 386,4 | 388 / 387 × 728 | 1,25 |
| Empilés | 1 172 × 360,8 | 1 172 × 362 | 1,25 |
| Empilés, zoom 125 % | 1 172 × 360,8 | 937 × 289 (= DIP / 1,25) | 1,5625 |

L'écart d'1 px vient de l'arrondi du moteur. Le test échoue au-delà de 2 px.

### Écarts assumés par rapport au flow

- **Bande de tension (40–84).** L'onglet reste dans la barre et réordonne ; il ne suit
  pas verticalement avec résistance. Il se soulève à y ≥ 84 et se rattache sous 64.
- **Pages pendant les animations.** Les WebView ne glissent pas : les bounds changent
  une seule fois au dépôt, à l'inversion ou au repli. Seuls l'onglet soulevé et
  l'aperçu sont animés (ressorts critiques, entrée 0,20 s, retour 0,35 s, fondu de
  sortie 0,12 s).
- **Minimum au pointeur.** Le séparateur s'arrête net à 360 px sans rubber band : une
  WebView ne peut pas descendre sous son minimum pour rebondir. Le ressort reste dans
  le modèle pour l'aperçu.
- **Cible du séparateur : 16 DIP, pas 24.** Voir « Grips » : 24 DIP auraient couvert
  8 DIP de chaque page, dont une bonne partie de leur barre de défilement. Le chemin
  clavier (« Redimensionner les panneaux ») reste l'équivalent prévu par WCAG 2.5.8.
- **Coins des pages.** Ils restent droits (HWND enfants).
- **Relâcher entre 64 et 84 DIP** pendant un soulèvement annule (retour à la place
  d'origine) au lieu de réordonner.
- **Séparateurs pour les lecteurs d'écran.** Il n'y a pas d'élément UIA RangeValue
  dédié. Le mode clavier annonce « Séparateur, Youtube 59 % » à chaque pas, et les
  onglets annoncent « vue partagée, 2 sur 3 » et « replié ».
- **Correction au passage.** Échap pendant un réordonnancement restaure maintenant
  l'ordre d'origine (c'était un bug existant).

### Vérification

- `tests/split-view/split_view_test.cpp` : unités, capacité sans plafond (736 → 2,
  1 180 → 3, 1 472 → 4, 2 560 → 6, 3 440 → 9, 5 120 → 13), zoom 125 %, rails à
  l'étroit, diagonales et hystérésis, refus, insertion, remplacement, échange,
  déplacement, bornes, aimants et ressorts.
- `tests/ui/split_ui_test.cpp` : par la vraie `WndProc`, réordonner sans split,
  soulever et déposer, Échap, insérer, refuser, remplacer sans perdre d'onglet, rail
  puis réouverture, redimensionnement clavier et pointeur, fermeture avec relais au
  voisin, empilement par le menu, inversion, F6, session, onglet seul.
- `python tests/update-integration/run_all.py` : suites existing, peers et
  integration toutes vertes, dont les deux nouvelles fixtures. Le smoke test de bout
  en bout passe aussi (`browser.exe --smoke-test`).
- **Test natif réel** `browser.exe --split-test` (via `test-split-view.ps1 -Native`) :
  - 4 pages locales `file://`, sans aucune requête réseau ;
  - glisser réel par messages souris : paire, insertion, refus d'un 4ᵉ site, aperçu
    « remplacer » puis Échap, rail à 900 DIP, redimensionnement clavier, empilement
    par le menu, zoom 125 %, focus d'une page devenue active, fermeture ;
  - pour chaque état, les bounds et la visibilité réels des contrôleurs sont
    comparés à la disposition.
- **Captures natives** (fenêtre rendue par `PrintWindow`, surimpression composée) dans
  `build/split-view-native/` : `01-deux-sites-cote-a-cote`, `02-trois-sites`,
  `03-refus-pas-de-place`, `04-apercu-remplacer`, `05-rail-fenetre-etroite`,
  `06-redimensionner-clavier`, `07-empiles`.

### Nom court du site sur la pastille et la vignette

La pastille d'aperçu et la vignette qui suit le pointeur affichaient le titre HTML
complet (« GitHub · Le changement est constant. … · GitHub · GitHub »). Elles affichent
désormais un nom de site compact (`src/site-name.h`, `SiteName::Compact`), calculé de
façon déterministe et locale à partir de l'URL et du titre, sans réseau ni script :

1. Une table courte de sites connus, sur l'hôte puis sur le domaine enregistrable :
   GitHub, YouTube (youtube.com, m., youtu.be), MDN, Wikipédia, etc.
2. Sinon, le segment du titre (séparé par ·, |, -, —, –, •) qui nomme le domaine, avec
   l'orthographe du site : « BBC News » pour bbc.co.uk, « Le Monde.fr » pour
   lemonde.fr. La marque n'est jamais supposée être le premier segment.
3. Sinon, le libellé du domaine capitalisé (« Foo-bar » pour foo-bar.io), l'hôte pour
   localhost et les IP, le nom du fichier pour une page locale, et en dernier recours le
   premier segment du titre.

Le titre complet n'est pas modifié : il reste sur l'onglet, dans l'historique et dans le
nom accessible de l'onglet. Les annonces UIA pendant le glisser utilisent le nom court,
plus lisible à l'oral. Les noms longs restants s'arrêtent sur une ellipsis : vignette de
128 DIP, pastille plafonnée à 240 DIP. Vérification : `tests/split-view/site_name_test.cpp`
(titres GitHub longs et localisés, variantes YouTube, segment, domaine, localhost, IP,
fichier, about:, data:), la fixture `split_ui_test` (vignette et pastille à « GitHub »,
titre de l'onglet intact), et le test natif (« SPLIT names », capture
`04-apercu-remplacer`).

### Correctifs après la preview (traînées, poignée, pastille)

**Traînées bleues et zones noires pendant le redimensionnement : cause et correctif.**
Quand une page s'agrandit, Chromium n'a pas encore rendu la nouvelle surface, et sa
partie non rendue laisse voir la surface de la fenêtre parente dessous. Avec
`WS_CLIPCHILDREN`, le parent ne peignait jamais sous ses pages : il y restait les
anciennes poignées et anciens contours accent (les « lignes et poignées répétées »),
ou une surface jamais peinte (noir).

Correctif dans `ApplyPages()` : avant tout changement de bounds, le parent peint le
canvas (GDI, DC sans `DCX_CLIPCHILDREN`) sous l'union de l'ancien et du nouveau
rectangle de chaque page qui bouge. Les pages sont composées au-dessus du parent, et
une page redimensionnée se repeint entièrement de toute façon. Il n'y a donc ni
masquage ni délai : la zone pas encore rendue montre le canvas, jamais un reste.

**Preuve avant / après** (`browser.exe --split-test`, captures `PrintWindow`
analysées pixel par pixel contre la disposition) :
- **avant le correctif**, balayage lent : 98 pixels exactement `#3E67FF` à
  l'intérieur de la page de gauche pendant le glisser (l'ancienne poignée) ;
- **après le correctif**, sur trois exécutions complètes consécutives et chaque
  balayage, 0 noir, 0 traînée, 0 gap non canvas et 0 trou une fois posé. Les
  balayages couvrent : rapide (20 × 10 DIP / 8 ms) aller-retour, lent
  (40 × 3 DIP / 33 ms), trois sites, empilé rapide, Échap et capture perdue en plein
  glisser.

**Échap et capture perdue pendant un redimensionnement** restaurent les tailles d'avant
la pression (c'était absent).

**Poignée et gap.** Gap de 8 DIP (`Space::md`) au lieu de 6, poignée de 6 × 48 DIP
visible au repos en blanc 40 % (3,82:1), accent au survol, au glisser et au focus. Le
contour actif (1 + 2 DIP) tient dans le demi-gap de 4 DIP. Capacités recalculées :
2 sites dès 736 DIP, 4 dès 1 472, 6 à 2 560.

**Grips.** Au-dessus d'une page (fenêtre enfant d'un autre processus), la fenêtre
principale ne reçoit pas la souris. Pour élargir la cible au-delà du gap :
- une fenêtre enfant en couches de 16 DIP (gap + 4 DIP sur chaque page), à alpha
  1/255, se place au-dessus des pages : invisible mais testée au clic, elle porte le
  curseur ↔ et transmet le pointeur à la fenêtre principale ;
- Windows n'autorise les enfants en couches qu'aux exécutables déclarés compatibles
  Windows 8 ou plus : `src/browser.manifest` (compatibilité seule, sans common
  controls v6 ni changement DPI/UAC) est embarqué par `browser.rc` ;
- sans manifeste (fixtures de test), aucun grip n'est créé et le gap seul reste la
  cible ;
- vérifié sur de vraies WebView2 : à ±7 DIP du gap le grip est l'enfant le plus haut,
  à 12 DIP c'est la page, et un glisser commencé à 6 DIP dans la page déplace le
  séparateur.

**Isolation du test natif.** `--split-test` ignore la souris réelle et ne prend plus
la capture : l'utilisateur travaillait dans d'autres applications pendant les
exécutions, et ses mouvements faussaient le test tout en étant détournés. Le
double-clic sur un séparateur se mesure désormais en `GetTickCount()`.

### Lot retouches utilisateur : redimensionnement, focus, groupe, poignée, coins, menu V2

Preview : `build/split-dev-5/browser.exe`. `split-dev-4` était ouvert par l'utilisateur
et n'a pas été touché, pas plus que `split-dev`, `split-dev-2` et `split-dev-3`.

**Redimensionnement : placeholders opaques** (direction de l'utilisateur, planche 09 de
Paper ; elle remplace l'aperçu par snapshot et flou de la version précédente, retiré
avec sa capture, son cache et son flou).
- **Déclenchement.** Après un seuil de 3 DIP de glisser réel, et non à la simple
  pression : un clic ou un double-clic ne fait rien apparaître. Les deux pages voisines
  du séparateur sont alors gelées à leurs bounds, sans aucun reflow pendant le glisser.
- **Placeholders.** Des surfaces opaques #171717 suivent leurs nouveaux rectangles 1:1
  et cachent entièrement les sites. Au centre de chacune, l'identité du site :
  - le favicon réel, local et déjà chargé, en 36 DIP, rayon 4. Sans favicon, un carré
    neutre en blanc 8 %, jamais un faux site ;
  - à 12 DIP sous le favicon, le nom court (`SiteName`, « GitHub »), 16 DIP medium ;
  - à 4 DIP sous le nom, le domaine nu, 14 DIP. Pas de schéma, `www`, port, chemin,
    requête, ancre ni barre finale. Il n'y a pas de ligne de domaine pour un fichier
    local, `about:` ou les autres schémas : il n'est jamais inventé ;
  - nom et domaine en blanc 50 % (5,26:1 sur #171717 ; Paper : #FFFFFF80 à 90 %
    d'opacité, soit 4,48:1, relevé pour passer 4,5:1) et coupés par une ellipse quand
    ils dépassent ;
  - l'identité est relue à chaque image depuis l'onglet : une navigation en plein
    glisser montre le nouveau site, jamais l'ancien.
- **Séparateur actif.** Il passe à l'accent #3E67FF : une ligne de 2 DIP sur toute sa
  longueur et la poignée 8 × 36. Les autres séparateurs restent neutres. Au repos, rien
  ne change : gap de 4 DIP et poignée visible seulement au survol. Le contour du
  panneau actif s'efface tant que les placeholders sont affichés, comme dans la
  planche 09.
- **Remplissage.** Une seule surface couvre les deux placeholders et le gap. Elle est
  élargie d'1 DIP et aliasée : sinon, un bord antialiasé sur un pixel fractionnaire
  laissait voir la page gelée en liseré gris (vu sur le balayage empilé).
- **Relâchement.** Les vrais bounds sont appliqués, puis chaque page est sondée par
  `CapturePreview`, en local et sans réseau. Seul l'en-tête PNG est lu, pour la
  taille, et rien n'est conservé. Quand la capture revient à la nouvelle taille, la
  page est rendue (62 à 157 ms mesurés) et les placeholders s'effacent en 120 ms ;
  instantanément en mouvement réduit. Au plus 20 essais, puis l'effacement a lieu
  quand même.
- **Échap ou capture perdue.** Les tailles d'avant la pression reviennent, et les
  placeholders disparaissent : les pages n'ont jamais bougé.
- **Zoom.** Le zoom de page n'est jamais touché.
- **Crash corrigé en QA, version snapshot.** Une tuile gardait un pointeur vers un
  snapshot libéré (trouvé avec gdb). Le problème disparaît avec les snapshots.

**Coins bas des pages arrondis.**
- **Cause.** Les pages sont des fenêtres enfants rectangulaires d'un autre processus.
  Le cadre de la fenêtre est arrondi, mais rien ne découpait les pages, donc leurs
  coins du bas restaient droits.
- **Correctif.** Sur chaque coin extérieur bas, une petite fenêtre enfant en couches
  (`UpdateLayeredWindow`), placée au-dessus de la page. Elle peint le canvas hors d'un
  arc de 6 DIP (`SV::pageRadius`, planche 08), antialiasé, et reste transparente à
  l'intérieur. La page y est réellement cachée, et le pointeur y passe à la page.
- **Où.** Page seule : ses deux coins du bas. Côte à côte : le coin bas gauche du
  premier panneau et le coin bas droit du dernier. Empilé : les deux coins du panneau
  du bas. Les coins du haut touchent la barre d'outils et restent droits, comme dans
  Paper. Les rails sont dessinés par le chrome, déjà arrondis.
- **Contour actif.** Sur ces coins, il suit l'arc (rayon 8 sur sa ligne centrale).
- **Placeholders.** Ils portent les mêmes coins.
- **Suivi.** Les coins sont recalculés à chaque placement de page : redimensionnement,
  DPI, maximisation (le rayon est gardé quand la fenêtre est maximisée).
- **Écart avec Paper.** Le rayon est fixé à 6 alors que la fenêtre Windows 11 arrondit
  à 8.
- **Pourquoi pas `SetWindowRgn`.** Il aurait fallu reparenter chaque WebView2 dans un
  conteneur, avec un bord crénelé (sans antialias) et une refonte de `ApplyPages`.
- **Vérifié en natif.** Page seule claire, paire claire / sombre, puis paire après un
  redimensionnement. Le pixel du coin est le canvas, la page est intacte dans l'arc.

**Menu : alignement.** « Avec un autre site », « Insérer entre », « Remplacer un site » et
les lignes de raison sont alignés sur le texte du menu, sans emplacement de glyphe vide
(planches V2 de p-7-0 réalignées par le designer). Seules les lignes de position (À
droite…, Ajouter au début / à la fin) portent un glyphe et son retrait. Sur un membre du groupe affiché, « Inverser,
Empiler, Redimensionner, Quitter, Avec un autre site › » forment un seul bloc (planche
12b du flow harmonisé).

**Focus du panneau actif, groupe dans la barre.** Voir les décisions 7 et 8. La
modalité vient de l'activation : un focus de page pris bouton enfoncé est un clic.

**Poignée visible seulement au survol.** Voir la décision 9. Les pages sont des fenêtres
enfants d'un autre processus : seul le grip en couches (au-dessus des pages) peut
dessiner ce qui les chevauche.
- **Rendu.** Le grip est rendu par `UpdateLayeredWindow` : alpha 1/255 partout pour
  rester touchable, et la poignée en distance signée antialiasée.
- **Fondu.** Opacité seule, 120 ms, sans effet sur la mise en page ; instantané en
  mouvement réduit.
- **Pendant un glisser.** La poignée reste affichée même si le pointeur quitte la cible
  (pas de clignotement). Au relâchement hors cible, elle s'efface.
- **Sortie.** La sortie est suivie sur le grip (`TME_LEAVE`). La fenêtre principale
  ignore son propre « leave » quand le pointeur entre dans le grip, qui est son enfant.
- **Focus clavier.** Anneau de 2 DIP en blanc 50 %, décalé d'1 DIP.
- **Repli sans grip.** Sans fenêtre enfant en couches, la poignée est peinte dans le
  gap.
- **Plusieurs séparateurs.** Seul celui qui est concerné apparaît.

**Menu d'onglet V2** (Paper p-7-0, planche « V2 · Bloc à plat » ; spec du designer dans
[split-view-drop-explorations.md](split-view-drop-explorations.md), « Spec V2
retenue »). Le bloc remplace les quatre lignes « Placer … de X », au même endroit, sans
raccourci.
- **En-tête non focalisable** (28 DIP, 13 DIP à 60 %). Favicon de 14 et « Avec
  {nom court} · affiché » (« · affiché » à 50 %), ou trois mini-favicons et « Avec la vue partagée · N
  sites ».
- **Cible par défaut.** La page affichée. Plus de « dernier consulté ». Si l'onglet
  cliqué est l'affiché (ou membre du groupe affiché), il n'y a pas de cible par défaut :
  seulement « Avec un autre site › ». Avec un seul onglet, le bloc est absent.
- **Onglet libre.** Quatre lignes « À droite / À gauche / Au-dessus / En dessous », avec
  un glyphe 18 × 14 (contour 1,5, moitié pleine, 50 %, 70 % au survol).
- **Groupe.** « Ajouter au début », « Ajouter à la fin » (glyphes de bout selon
  l'orientation), « Insérer entre › » (« Entre A et B ») et, séparé, « Remplacer un
  site › ». Pas de haut ou de bas sur un groupe côte à côte.
- **Indisponible.** Lignes à 32 %, non activables, suivies d'une ligne de raison
  non focalisable, alignée sur le texte (13 DIP, #FFFFFFB3) : « Pas de place pour un 4ᵉ site · 360 px minimum »,
  « Pas de place côte à côte · 360 px minimum » ou « Pas de place pour empiler · 256 px
  minimum ».
- **« Avec un autre site › ».** Tous les onglets dans l'ordre de la barre, sauf la source
  et la cible de l'en-tête. Un groupe est une entrée « Vue partagée · N sites ». Chaque
  entrée ouvre les mêmes positions. Toutes les lignes exécutent une opération existante
  (`ApplySplitTarget` : paire, insertion, remplacement), y compris vers un onglet ou un
  groupe hors écran, qui devient alors l'affiché.
- **Homonymes.** Nom court et un second mot à 50 %. Le second mot est le dernier segment
  du chemin s'il les distingue (« GitHub Browser » / « GitHub Issues »), sinon le rang
  dans la barre (« GitHub 1 » / « GitHub 2 »).
- **Survol et focus.** Survol : fond #FFFFFF14 seul. Focus clavier : même fond et
  anneau intérieur d'1 DIP en blanc 50 %. Parent d'un sous-menu ouvert : fond seul.
  ↑ ↓ sautent l'en-tête et la raison, → ouvre, ← et Échap remontent, Entrée valide.
- **Désignation.** La ligne survolée ou focalisée désigne sa cible dans la barre : un
  liseré blanc 50 % d'1 DIP, sur l'onglet (rayon 6) ou autour du groupe (2 DIP hors
  des onglets, rayon 8). Le liseré s'efface à la fermeture du menu.
- **Accessibilité.** Le menu natif n'expose pas d'arbre UIA (limite antérieure). Chaque
  ligne porte un nom accessible (« À droite, Avec YouTube ; indisponible : … ») annoncé
  par notification UIA quand elle est mise en évidence.
- **Pas fait, volontairement.** L'aperçu optionnel sur la page (point 12), à valider
  par l'utilisateur. Rien de V3, aucun changement du geste de split.

**Défaut corrigé en passant.** `ApplySplitTarget` recevait ses onglets par référence.
Un appelant qui passait `pages[i]` voyait l'onglet visé changer pendant le
réordonnancement : l'harnais natif ciblait Paper au lieu de GitHub. Elle prend
maintenant ses propres références.

**Vérification de ce lot.**
- `test-split-view.ps1 -Native -Out build/split-dev-5` : PASS.
- Fixtures : modèle au gap de 4, poignée au repos / au clavier, menu V2 (cible,
  autres sites, homonymes, raison groupe plein, remplacement, groupe hors écran).
- Étapes natives :
  - poignée : repos, survol 140 ms, sortie, glisser hors cible maintenu, clavier,
    capture perdue ;
  - menu V2 : onglets libres + survol, groupe plein, autre site au clavier qui empile
    réellement Paper au-dessus de GitHub.
- Coins : page seule, paire claire / sombre, paire après redimensionnement.
- Placeholders : balayages 08, 09, 11 (trois sites) et 12 (empilé), Échap et capture
  perdue, nom court GitHub, domaine nu et domaine long coupé.
- Captures `08`–`21` dans `build/split-dev-5/split-qa`.

## Préparation technique (historique)

Constats faits avant l'implémentation, conservés pour la traçabilité :

- **Hébergement WebView2 fenêtré.** `CreateCoreWebView2Controller(window, …)`
  (`main.cpp`, `NewTab`) : chaque page est un HWND enfant, et le parent ne peut rien
  peindre par-dessus. L'onglet soulevé, la plaque et l'ombre exigent donc une fenêtre
  d'overlay au-dessus du contenu. Recommandation : popup sans redirection
  (`WS_EX_NOREDIRECTIONBITMAP`) + DirectComposition, alpha par pixel et transform GPU,
  créée au soulèvement et détruite au relâchement, donc rien au repos. Une alternative
  plus lourde serait de migrer vers `CreateCoreWebView2CompositionController`. Le
  contour actif et le gap se peignent dans le canvas du parent, autour des HWND
  (`WS_CLIPCHILDREN` est déjà actif).
- **Cible du séparateur.** Le gap de 6 DIP n'atteint pas 24 DIP. Il faut une petite
  fenêtre enfant transparente au-dessus des WebView, de 24 DIP de large, qui porte le
  curseur ↔, le glisser et le focus clavier.
- **Coins arrondis.** Un HWND enfant reste rectangulaire. Les rayons de 6 du design
  demandent soit un clip DirectComposition (hébergement composition), soit d'accepter
  des coins droits en natif. C'est à trancher pendant le spike. *Résolu depuis* : des
  masques en couches aux coins extérieurs bas (voir le lot retouches utilisateur).
- **Glisser actuel** (`DragTab`, `main.cpp`). Le réordonnancement est appliqué en
  direct par `MoveTab`, et `CancelTabDrag` ne remet que l'offset. Aujourd'hui, Échap
  ne restaure donc **pas** l'ordre d'origine. Le flow l'exige (étape 14) : il faudra
  mémoriser l'index de départ dans `TabDrag`.
- **Ressorts.** `downloads.h` a un `Spring` critique mais toujours **depuis le
  repos** (vitesse initiale nulle). Le flow demande la reprise de vitesse au
  relâchement. Il faut un ressort critique à état (position, vitesse, cible) dans
  `motion.h`, réutilisable par la barre, l'overlay et le séparateur. Le compositor
  clock existant (`frame-clock.h`) cadence déjà les frames à la demande.
- **Pages en veille.** `TrySuspend` endort les onglets cachés, et
  `put_IsVisible(TRUE)` les réveille. Le panneau partenaire ne doit jamais être
  candidat à la veille.
- **Session.** `browser-session-1` n'enregistre que des URL et l'index actif. Il
  faudra un format 2 : paire (index gauche/droite), proportion, panneau actif. La
  lecture du format 1 doit rester possible.
- **Accessibilité.** `accessibility.h` expose Tab/TabItem/List/Button/Edit/ToolBar. Il
  faudra un groupe « Vue partagée » dans la liste d'onglets et un séparateur avec
  `IRangeValueProvider`, plus les annonces listées en 15.

Découpage proposé, dans l'esprit des fixtures existantes :

1. `src/split-view.h`, en-tête pur sans Win32. Il contient le modèle du groupe
   (orientation, sites ordonnés, fractions, MRU, rails) et la capacité
   `n max(contenu, zoom)`. Il calcule aussi les zones (triangles, centre/quarts,
   hystérésis, refus), les rects des panneaux et des rails, l'insertion
   1 / (n + 1), le rubber band, la projection et les aimants, et l'ordre du groupe
   dans la barre. Couvert par
   `tests/split-view/` comme `tests/ui/tab_drag_test.cpp`.
2. Ressort à vitesse dans `motion.h`, avec tests de reprise en vol.
3. Spike d'overlay DirectComposition au-dessus de deux WebView : mesurer l'absence de
   flash au dépôt et le coût par frame.
4. Intégration `main.cpp` : `Layout()` à n bounds (rails masqués via `put_IsVisible`), `Active()` = panneau actif,
   commandes et adresse routées, menu d'onglet, F6, session 2, UIA.

## Vérification de ce livrable

- Paper : 17 artboards sur une page neuve, construits par clonage du chrome V3 et du
  vrai menu d'onglet (« Clic droit onglet — Imprimer / PDF »). Une capture et une
  critique après chaque étape, corrections ciblées (chevauchements, libellé d'aperçu
  sous le pointeur, contour actif invisible, menu cloné entré dans le flux,
  contraste du sous-titre, flèche d'état erronée, espaces insécables). Nœuds libérés
  (`finish_working_on_nodes`).
- Extension orientations et capacité : `build/split-view-paper/extension/00.jpeg` …
  `23.jpeg`, état final des 25 planches. Chaque nouvelle planche a été relue en
  capture. Corrections faites : étiquettes de 16, 17, 18 et 22 déplacées ; pointeur
  et pastille de refus qui masquaient les titres ; « 4ᵉ » ; planche partielle 16
  supprimée et reconstruite après une erreur de script.
- Captures accent : `build/split-view-paper/accent/00.jpeg` … `15.jpeg` (version
  accent). La première version grise reste dans `build/split-view-paper/`. Ces dossiers
  sont ignorés par git, comme les preuves Paper précédentes.
- Passe accent relue écran par écran. Corrections faites : contour 1 DIP trop faible
  passé à 2 DIP, double accent pendant un remplacement (contour masqué pendant le
  glisser), panneau sortant teinté trop lourd (rendu à la page qui s'efface), ligne
  clavier tronquée en 15.
- Connexion : le MCP Paper de la session refusait la connexion au démarrage (Paper
  Desktop fermé). Paper Desktop a été lancé, puis son serveur MCP local
  (`127.0.0.1:29979`) a été appelé directement en HTTP, guide
  `paper-mcp-instructions` chargé en premier.

## Limites

Limites de l'implémentation :

- Ce qui précède dans « Écarts assumés » : pas de bande de tension animée, WebView non
  animées, arrêt net au minimum, cible pointeur de 16 DIP (24 au clavier), coins hauts droits (bas arrondis à 6), pas d'élément
  UIA dédié aux séparateurs.
- Le test natif a tourné à DPI 120 seulement ; 96 et 144+ ne sont couverts que par le
  modèle et les fixtures. Les écrans ultra-wide (6 sites et plus) sont couverts par
  le modèle, pas par un écran réel.
- Pas de mesure de coût CPU/GPU avec six WebView visibles redimensionnées en direct.
- Pas de vraie sortie lecteur d'écran écoutée (Narrateur/NVDA) : les annonces sont
  émises par UIA Notification, comme les annonces existantes.

Limites du flow de design (historique) :


- Flow statique : aucun prototype jouable. Les durées et les ressorts sont spécifiés,
  pas encore ressentis à la main.
- Aucune mesure de performance. Le coût du redimensionnement en direct de deux WebView
  et de l'overlay DirectComposition n'est pas mesuré, d'où le spike de l'étape 3.
- Les pages sont des placeholders gris. Le contraste du contour et de la plaque est
  calculé, pas mesuré sur de vraies pages.
- PiP n'est pas couvert par ce flow, malgré le nom de la branche.
- Les valeurs 360 × 256 px CSS sont une proposition argumentée, pas une mesure sur des
  sites réels. Leur comportement à ces tailles n'a pas été testé.
- Les captures ultra-wide (2 560 × 1 080) sont des maquettes. Le coût de six WebView
  visibles et redimensionnées ensemble n'est pas mesuré.
- Écrans 01 à 15 : les étapes 05, 06, 08, 09, 10, 12a, 12b et 15 ont été mises en
  cohérence (diagonales, 360 px, F6, menus). Les autres écrans à deux sites restent
  valables tels quels.
