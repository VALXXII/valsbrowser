# Vue partagée — menu clic droit d'onglet : variantes

État : **V2 retenue par l'utilisateur** (8 novembre). L'implémentation est confiée au
premier agent. V1 et V3 restent des explorations, **non validées** : la croix V3
n'est pas retenue, et « Avec un autre site » suit le modèle V2 en sous-menus
(site › position), voir « Spec V2 retenue » ci-dessous. Aucune implémentation modifiée
par cette note. Ni les zones de split, ni le
flow validé, ni le geste de dépôt ne changent. Cette note ne remplace pas
[split-view.md](split-view.md) : elle propose des directions pour une seule surface,
le bloc « Placer à droite / à gauche / au-dessus / en dessous de … » du menu clic
droit d'onglet (le « dropover » de la demande).

Paper, fichier *Browser Design* :

- **Livrable principal** : page **Split View — menu clic droit onglet (variantes)**,
  https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-7-0
- Archive, **hors périmètre actuel** : page **Split View — archive dépôt (A/B/C hors
  périmètre)**, https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-6-0. Elle
  contient les explorations du geste de dépôt (cibles sur panneau, bords et
  séparateurs, plan de disposition), conservées mais non retenues dans ce cadrage.

## Point de départ

Capture utilisateur : menu clic droit sous l'onglet Paper. Le bloc « Placer à
droite / à gauche / au-dessus / en dessous de Youtube » se trouve entre « Copier le
lien » et « Imprimer… », avec les raccourcis en touches (Ctrl T, Ctrl R, Ctrl P,
Ctrl Maj T, Ctrl W). Une autre capture montrait le même bloc nommant
« x at DuckDuckGo ». Problèmes :

- la cible est implicite (l'actif, sinon le dernier consulté) ;
- elle porte le titre HTML de la page ;
- aucune autre cible ne peut être choisie quand plusieurs onglets sont ouverts.

## Contexte commun des maquettes

- Fenêtre réelle de 1 180 DIP : barre d'onglets, adresse, page.
- Deux contextes :
  - **onglets libres** : YouTube affiché, Paper (clic droit), deux GitHub
    (Browser / Issues), Luma ;
  - **groupe plein** : YouTube | GitHub | Luma, côte à côte, avec Paper et DuckDuckGo
    libres.
- Les onglets du groupe suivent la retouche utilisateur : contours neutres, coins
  extérieurs arrondis, coins intérieurs droits, petit séparateur. Le focus à la souris
  est discret ; le focus clavier garde un anneau blanc de 50 %.
- Menu complet reproduit, avec les mêmes séparateurs, raccourcis, hauteurs (32 px)
  et survol (#FFFFFF14).
- **Nom court partout** (« YouTube », « GitHub », « Luma ») ; un second mot en gris
  distingue les homonymes (« GitHub Browser » / « GitHub Issues »).
- Cible désignée dans la barre par un liseré blanc de 50 % sur son onglet ou son
  groupe, sans accent.
- **Aperçu au survol, optionnel** : voile #3E67FF sur le rect final et pastille au
  nom court « Paper » (décision existante, pas de phrase de position dans la
  pastille). Il n'apparaît que si la cible est affichée ; sinon, seul l'onglet cible
  est désigné. Le comportement du split ne change pas.

## Variantes (3 états chacune : onglets libres, groupe plein, clavier)

### V1 · Texte, cible d'abord (sous-menus)

« Placer à côté de › [site] › [position] ». Tout est en menus Win32 standards :

- le site affiché est marqué « affiché » ;
- un groupe apparaît comme une seule entrée « Vue partagée · 3 sites » ;
- les positions d'un groupe plein (« Avant YouTube / Entre … et … / Après Luma ») sont
  à 32 %, avec la raison sur sa propre ligne ;
- « Remplacer … » forme une section séparée.

Au clavier : Maj+F10, flèches, Échap ou ← remonte d'un niveau.

Avantages : très standard, coût natif quasi nul, une seule ligne dans le menu.
Inconvénient : trois niveaux pour le cas le plus fréquent.

### V2 · Bloc à plat, site nommé en en-tête

On garde le bloc à sa place :

- un en-tête non cliquable « Avec YouTube · affiché » (favicon et nom court) ;
- quatre lignes courtes « À droite / À gauche / Au-dessus / En dessous », chacune
  avec un glyphe de disposition et son texte ;
- « Avec un autre site › » ;
- pour un groupe plein : « Ajouter au début / à la fin / Insérer entre › »
  indisponibles avec leur raison, plus « Remplacer un site › » séparé.

UIA lit « À droite, Avec YouTube ».

Avantages : un clic pour le cas courant, la cible écrite une seule fois. Inconvénient :
deux lignes de plus.

### V3 · Choisir le site, puis la croix (piste utilisateur)

« Placer à côté de… » ouvre un panneau de 332 px collé au menu, sans overlay sur la
page. De haut en bas :

1. la source explicite : « Placer [Paper] à côté de » ;
2. la liste des sites (favicon, nom court, second mot, coche) ;
3. la croix spatiale : le centre est la cible, en pointillés, non focalisable et non
   cliquable. Il n'y a donc jamais de remplacement par accident. Autour, quatre
   boutons libellés (92 × 52 et 124 × 36 px) ;
4. la ligne de résultat : mini vue 48 × 30 et « Paper à droite de YouTube », Entrée.

Cas particuliers :

- **Groupe plein** : directions à 32 %. La raison est écrite : « Pas de place pour un
  4ᵉ site · 360 px minimum chacun. Haut et bas : la vue est côte à côte. » La commande
  séparée « Remplacer un site du groupe… » reste disponible.
- **Clavier** : ↑↓ dans la liste, Tab vers la croix, flèches spatiales, Entrée.
  Échap revient au menu, un second Échap le ferme. Focus : anneau blanc de 50 %
  décalé de 2 px.

Avantages : le plus explicite (source, cible, direction, résultat), avec tous les
états au même endroit. Inconvénients : composant sur mesure, plus dense, en deux
temps.

## Évaluation

| Variante | Cas courant | Choix du site | Compréhension spatiale | Densité | Coût natif |
| --- | --- | --- | --- | --- | --- |
| Aujourd'hui | 1 clic | implicite | faible | 4 lignes longues | — |
| V1 | 3 clics | 5/5 | 3/5 | 1 ligne | très faible |
| V2 | 1 clic | 4/5 | 4/5 | 6 lignes | faible |
| V3 | 2 clics | 5/5 | 5/5 | 1 ligne + panneau | moyen |

## Spec V2 retenue

Références Paper (page p-7-0, planche « V2 · Bloc à plat, site nommé en en-tête ») :
V2-1 (onglets libres, survol), V2-2 (groupe plein), V2-3 (autre site au clavier).

1. **Place dans le menu.** Le bloc remplace les quatre lignes « Placer … de X »,
   entre « Copier le lien » et « Imprimer… ». Les séparateurs et raccourcis existants
   ne changent pas, et le bloc n'a pas de raccourci.
2. **Cible par défaut.** C'est la page affichée : l'onglet actif, ou son groupe s'il
   est en vue partagée. Plus de règle « dernier consulté ». Si l'onglet cliqué est
   lui-même l'affiché, le bloc ne vise rien par défaut et ne montre que « Avec un
   autre site › ». Avec un seul onglet, le bloc est absent.
3. **En-tête** (non focalisable, non cliquable, 28 px) : favicon 14 px et
   « Avec {nom court} », suivi de « · affiché » à 50 % de blanc (#FFFFFF80, 5,26:1 ; 35 % ne donnait que 3,2:1). Texte 13 px à 60 %
   de blanc (#FFFFFF99). Pour un groupe, trois mini-favicons et « Avec la vue
   partagée · N sites ». Le nom court vient de `SiteName`, jamais du titre HTML.
4. **Lignes.** Onglet libre : « À droite », « À gauche », « Au-dessus »,
   « En dessous », chacune précédée d'un glyphe de disposition 18 × 14 (contour
   1,5 px, moitié pleine). Groupe : « Ajouter au début », « Ajouter à la fin »,
   « Insérer entre › » (sous-menu « Entre A et B »), puis « Remplacer un site › »
   (sous-menu par site), toujours séparé : aucun remplacement implicite.
5. **Survol et focus.** Survol : fond #FFFFFF14 seul. Focus clavier : même fond et
   anneau intérieur blanc de 50 % (1 px). Sous-menu ouvert : parent en fond, sans
   anneau.
6. **Indisponible (manque de place, orientation).** Lignes à 32 %, non
   activables, suivies d'une ligne de raison non focalisable en 13 px,
   #FFFFFFB3 : « Pas de place pour un 4ᵉ site · 360 px minimum ». Elle est reliée
   aux lignes par la description UIA. Haut et bas sur un groupe côte à côte :
   absents, ou indisponibles avec leur raison.
7. **« Avec un autre site › ».** Sous-menu des onglets dans l'ordre de la barre, sauf
   la source et la cible déjà nommée en en-tête. Chaque site ouvre un sous-menu de
   positions identique au bloc. Un groupe apparaît comme une seule entrée
   « Vue partagée · N sites ».
8. **Homonymes.** Nom court, puis un second mot à 50 % de blanc (#FFFFFF80, 5,26:1 ; 45 % ne donnait que 4,48:1) :
   « GitHub Browser », « GitHub Issues ». La source du second mot reste à décider ;
   à défaut, un numéro d'ordre.
   Alignement (retour utilisateur, implémenté) : seules les lignes de position ont
   un glyphe et un retrait. « Avec un autre site › », « Insérer entre › »,
   « Remplacer un site › » et la ligne de raison sont alignées sur le texte du menu,
   sans emplacement vide (il évoquait une icône manquante). Planches V2 réalignées.
9. **Clavier.** Menu par Maj+F10 ou la touche Menu. ↑↓ parcourt les lignes et
   saute l'en-tête et la ligne de raison. → ouvre un sous-menu, ← ou Échap remonte
   d'un niveau, Entrée valide. Aucun nouveau raccourci global.
10. **UIA.** Chaque ligne est lue avec sa cible : « À droite, Avec YouTube ». Les
    lignes indisponibles portent leur raison.
11. **Désignation de la cible.** Au survol ou au focus d'une ligne, l'onglet ou le
    groupe cible reçoit un liseré blanc de 50 % dans la barre.
12. **Aperçu sur la page.** Il est optionnel et n'est pas exigé : à valider avec
    l'utilisateur. S'il est fait, il n'apparaît que pour une cible affichée : voile
    accent sur le rect final et pastille au nom court de la source. Le comportement
    du split ne change pas.

## Recommandation initiale (avant décision)

**V2 pour le cas courant, et la croix V3 derrière « Avec un autre site… ».**

- V2 corrige les deux défauts de la capture (cible implicite, titre long) avec le
  moins de changements, et garde le cas courant à un clic.
- La croix V3 est la plus claire pour « où » et « avec qui ». On la réserve au cas
  rare où il faut choisir un autre site, ce qui limite son coût de densité et
  d'apprentissage.
- V1 reste le repli si un panneau sur mesure est jugé trop coûteux.
- Aucune variante ne remplace par accident : « Remplacer » est toujours une commande
  à part.

Compromis :

- deux lignes de plus dans le menu ;
- pour le panneau V3, focus, UIA et positionnement près des bords d'écran sont à
  écrire ;
- l'aperçu sur la page est limité aux cibles affichées.

## Questions de décision

1. Cible par défaut du bloc : le site ou le groupe affiché (proposé), ou le voisin
   dans la barre ?
2. Panneau sur mesure V3 pour « Avec un autre site… », ou sous-menus Win32 (V1) ?
3. Quatre directions à plat dans V2, ou seulement gauche et droite à plat, et haut et
   bas dans la croix ?
4. Aperçu au survol sur la page : à garder, ou désigner seulement l'onglet cible ?
5. Homonymes : second mot tiré du titre, du chemin, ou de l'ordre (« GitHub 2 ») ?

## Limites

- Maquettes statiques : aucun prototype jouable, aucun test utilisateur.
- Contrastes repris des mesures existantes (anneau blanc 50 %, raison #FFFFFFB3 à
  #FFFFFFCC sur #171717). Ceux du panneau V3 ne sont pas mesurés sur rendu natif.
- La position du panneau V3 près du bord droit de l'écran (retournement à gauche)
  n'est pas dessinée.
