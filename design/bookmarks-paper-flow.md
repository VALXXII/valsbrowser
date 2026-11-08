# Favoris — flow Paper

Date : **8 novembre 2026**. Livrable : concept Paper **statique** (storyboard), pas de prototype interactif. Aucun code applicatif modifié.

- Fichier Paper : **Browser Design** (`01M6Q7YT14ZKMGET54ASPNGQEC`)
- Nouvelle page : **Favoris — flow** — <https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-8-0>
- Les pages existantes (Page 1, Split View — flow, menu clic droit, archive, Design system) n'ont pas été modifiées. Les clones viennent de « Base · onglets libres » (page menu clic droit) et ont été modifiés **uniquement** sur la nouvelle page.

Sources de travail lues : `design/design-system.md`, `design/bookmarks-research.md` (worker recherche), `design/bookmarks-implementation-plan.md` (worker plan), skills `apple-design`, `better-ui`, `better-interface` + les six `better-*` et `review-format.md`. Source applicative lue avant toute affirmation : `src/chrome.h` (régions toolbar `'Y' 'D' 'P' 'S'`, l. 386), `src/context-menu.h` (en-tête et constantes l. 1-60), `src/history-panel.h`, `src/downloads.h` (`Panel`, `DrawerLayout`), `src/main.cpp` (menus FR, `S(84)` l. 127 et 207, F6 l. 350, raccourcis À propos l. 1343).

## Inventaire des frames (14 artboards, 1 260 px de large)

| # | Artboard | Contenu |
|---|---|---|
| 00 | Favoris — vue d'ensemble | Parcours en pastilles, tableau des 11 décisions |
| 01 | Barre vide | Barre affichée sans favori : une phrase + « Ajouter cette page Ctrl+D » ; étoile vide |
| 02 | Ajouter : Ctrl+D ou étoile | Étoile pleine, popover « Favori ajouté » (320 DIP) ancré à l'étoile : Nom (focus), Dossier, Supprimer / Terminé |
| 03 | Destination et nouveau dossier | Arbre des dossiers en ligne, ligne « Veille » créée en renommage, « Nouveau dossier » |
| 04 | Enregistré | Bande avec le dossier « Veille » (icône + chevron 12 DIP à 32 %) |
| 05 | Menu de dossier et sous-menu | Bande peuplée, « Projet Lisière » ouvert en menu natif, sous-menu « Maquettes », « Ouvrir les 6 favoris » |
| 06 | Clic droit : actions d'un favori | Menu natif : Ouvrir dans un nouvel onglet, Modifier… F2, Dupliquer, Copier le lien, Déplacer vers ▸, Supprimer (Suppr, rouge), Nouveau dossier…, Masquer la barre de favoris Ctrl+Maj+B |
| 07 | Dupliquer et modifier | Copie insérée après l'original, popover « Modifier le favori » avec Adresse |
| 08 | Réordonner : glisser et clavier | Fantôme soulevé, emplacement ouvert ; bandeau « Sans glisser » avec focus clavier et Ctrl+Maj+←/→ |
| 09 | Supprimer et annuler | Dossier supprimé, voisins resserrés, toast « Dossier « Projet Lisière » supprimé (6 favoris) · Annuler Ctrl+Z » |
| 10 | Débordement et fenêtre étroite | Bouton » + menu des éléments cachés, Autres favoris, Gérer les favoris ; bande à 560 DIP (titres réduits à 96) |
| 11 | Retrouver : tiroir Favoris | Tiroir 268 DIP, recherche « monde », sections, lignes 48 DIP, focus sur une ligne |
| 12 | Erreurs et reprise | Cas A adresse invalide (icône + texte sous le champ) ; cas B échec d'écriture disque, toast persistant « Réessayer » |
| 13 | Clavier, focus, motion | Tables raccourcis et mouvement (normal / réduit / indice statique), bloquants et non vérifié |

Chaque scène 01-12 suit la convention de la page Split View : fenêtre 1 180 × 660 clonée du chrome V3, puis quatre colonnes (intention, Geste, Réponse et motion, Clavier et accessibilité) et des pastilles d'annotation magenta `#D4148C`.

Le chrome a été actualisé d'après `src/chrome.h:386`. La rangée droite contient Historique, Téléchargements, Navigation privée | Réglages. L'étoile est ajoutée dans le champ d'adresse, après Copier le lien.

## Décisions de design

Toutes sont alignées sur la recherche, le plan technique et les arbitrages du coordinateur (barre de 36 DIP, chevron sur les dossiers, Ctrl+Maj+←/→ dans la barre, Alt+↑/↓ dans le tiroir, nom UIA « Barre de favoris »).

1. **De vrais favoris, distincts des onglets.** Pas d'onglets épinglés ni de groupes vivants. Le modèle d'Arc (onglets épinglés = favoris) suppose une barre latérale, absente ici.
2. **Ajout en un geste.** Ctrl+D ou l'étoile enregistrent immédiatement. Le popover sert seulement à corriger.
   - Il fait 320 DIP, avec une coque de rayon 8 comme le popover Téléchargements, et il est ancré à l'étoile.
   - « Terminé » est le seul bouton plein. « Supprimer » est en rouge `#FF7B6E`, texte à l'appui.
3. **Destination en ligne.** L'arbre des dossiers s'affiche dans le popover, comme l'interstitiel de Dia 1.13.1, et « Nouveau dossier » crée une ligne en renommage. Ce choix est aussi justifié par l'accessibilité : une liste est exposable à UIA, alors que le menu natif ne l'est pas.
4. **Bande de 36 DIP.**
   - Éléments de 32 DIP, plus 4 DIP sous la bande, ce qui pose la page à 120 DIP.
   - Variante Ghost : sans fond au repos, survol `#333333`, rayon 6.
   - Texte à 50 %, favicon 16, écart 6, titre plafonné à 160 DIP.
   - La bande est masquée tant qu'elle est vide ; Ctrl+Maj+B la bascule.
5. **Dossiers.** Icône plus chevron 12 DIP à 32 %. Le menu natif s'ouvre en cascade, sous-dossiers d'abord, et se termine par « Ouvrir les N favoris ». Le dossier garde l'état survol tant que son menu est ouvert.
6. **Menus.** Ils reprennent le menu natif existant : coque 10 = 6 + 4, lignes de 32, séparateurs de 9, touches en pastilles et vocabulaire français du produit.
7. **Réordonner.** Le glisser se comporte comme un onglet : seuil de 4 DIP, fantôme sur `#292929`, voisins en ressort critique. Les alternatives sont Ctrl+Maj+←/→ (symétrique de Ctrl+Maj+PgPréc/PgSuiv pour les onglets) et « Déplacer vers ▸ ». Un dossier ne peut pas être déposé dans lui-même ni dans ses descendants.
8. **Supprimer.** Pas de confirmation. Un toast en bas à gauche (inset 14 DIP) nomme ce qui a été supprimé, avec le nombre de favoris, et propose « Annuler » ainsi que Ctrl+Z. Il dure 6 s et se suspend au survol ou au focus.
9. **Débordement.** Les titres passent de 160 à 96 DIP, effacés en fondu et jamais par « … ». Ensuite, les éléments restants vont dans un seul bouton `»`, dont la place est réservée d'avance. La bande ne défile jamais.
10. **Retrouver.**
    - Le tiroir Favoris fait 268 DIP et s'ouvre avec Ctrl+Maj+O. Il reprend le gabarit de l'Historique : en-tête, recherche, sections, lignes de 48 DIP.
    - La barre d'adresse propose aussi les favoris, au-dessus de l'historique. Ce point est décrit dans la scène, pas dessiné.
11. **Erreurs.**
    - Adresse invalide : la validation a lieu à l'envoi. Le message apparaît sous le champ, avec une icône et un texte qui dit quoi corriger, et le focus reste dans le champ.
    - Échec d'écriture : le toast persiste et propose « Réessayer ». Les données sont gardées en mémoire.
12. **Couleur.** Aucun nouvel accent. Le bleu `#3E67FF` reste réservé à la vue partagée.

## Sources externes (consultées le 2026-11-08)

- Dia, changelog officiel : <https://diabrowser.com/changelog>
  - 1.13.1 Mac : arbre de dossiers en ligne et débordement en un bouton.
  - 1.20.0 : barre de favoris sous la navigation.
  - 1.26.0 : annulation des actions groupées.
  - 1.39.0 : l'ordre manuel est préservé.
  - 1.5.0 Windows : « Open in New Tab » et clic milieu.
  - 1.54.0 : Cmd+Shift+D épingle.
- Dia, aide : <https://diabrowser.com/help>. La page est rendue en JavaScript et aucun article n'était lisible.
- Arc, « Where are Bookmarks » : <https://resources.arc.net/hc/en-us/articles/19400407903767-Where-are-Bookmarks>. Réponse HTTP 403 à la lecture directe ; seul l'extrait indexé est connu (« Arc for desktop doesn't have bookmarks – we used Pinned Tabs instead »).
- Le reste des sources Arc et Chrome (FAQ de fin d'Arc, migration Arc → Dia, raccourcis Chrome) figure, avec ses URL, dans `design/bookmarks-research.md`. Il n'a pas été relu ici.

## Revue better-interface (portée : le storyboard Paper « Favoris — flow »)

**Périmètre.** La revue couvre les 14 artboards de la page Favoris — flow, en état statique : il n'y a pas de code. La pile cible est le chrome natif Direct2D/DirectWrite et les tokens de `src/design-system.h` (doc : `design/design-system.md`). Pas de CONTRIBUTING, AGENTS ni CLAUDE.md dans le dépôt. Sont exclues l'implémentation, les suggestions de la barre d'adresse (non dessinées) et la scène Réglages.

| Domaine | Preuves inspectées | Résultat |
|---|---|---|
| Accessibilité | Spécifications clavier 01-13, focus dessiné en 02, 08 et 11, chemins sans glisser en 06 et 08, constat sur `context-menu.h` | 1 finding |
| Mise en page | Captures 01-12, bande à 560 DIP (10), position des menus et popovers | 1 finding |
| Rédaction | Libellés comparés à `main.cpp` (« Nouvel onglet », « Copier le lien », « Ctrl+Maj+T ») et au panneau Réglages | 1 finding |
| Typographie | Inter Display 14/16 et rôles repris des clones, troncature en 10 | Clear |
| Couleurs | Tokens existants, `#FF7B6E` réservé à la suppression, un seul bouton plein par vue | 1 finding (non mesuré) |
| UI | Rayons concentriques des menus (10 = 6 + 4), ombre et hairline, tableau motion en 13 | Clear |

| Sévérité | Domaine | Emplacement | Avant | Après | Pourquoi |
|---|---|---|---|---|---|
| HIGH | Accessibilité | 05, 06, 10 — menus natifs (`src/context-menu.h`) | Le menu natif n'expose rien à UIA ; les dossiers s'ouvrent dans ce menu | Exposer Menu/MenuItem et les événements MenuOpened/Closed avant de livrer ; le tiroir 11 sert de chemin de repli | Déclencheur : contrôle sans nom accessible, dossier inatteignable pour un lecteur d'écran (WCAG 4.1.2). C'est un écart existant que la fonctionnalité rend bloquant. |
| MEDIUM | Mise en page | 01-12 — page à 84 DIP (`main.cpp:127`, `:207`, `downloads.h:265/302`, `update-toast.h:13`) | Haut de page codé en dur | `ContentTop()` = 84 ou 120 selon la bande | Sinon la bande recouvre la page ou les drawers se décalent |
| MEDIUM | Rédaction | 02-12 — vocabulaire des menus | Historique et Téléchargements restent en anglais (« History », « View all ») alors que les favoris sont en français | Garder les favoris en français, comme les menus et les Réglages ; harmoniser les autres panneaux séparément | Deux langues dans le même chrome |
| LOW | Couleurs | 02, 07, 12 — bouton « Terminé » | Fond blanc plein : premier bouton primaire rempli du chrome | Le valider ou le remplacer par la variante Raised existante | Nouveau traitement absent de `design-system.h` ; contraste non mesuré |

**Vérification**

- Fait :
  - captures Paper après chaque groupe (01 à 13), corrigées pour les débordements de pastilles (03, 07, 10), le retour à la ligne d'un menu (06), l'extrait clavier (08) et le double focus (11) ;
  - libellés et raccourcis comparés à `src/main.cpp` ;
  - géométrie comparée à `src/downloads.h` et `src/context-menu.h`.
- **Non vérifié** :
  - interactions natives, timings, ressorts et mouvement réduit : décrits, jamais joués ;
  - contraste : aucune paire mesurée sur ces écrans. Le texte à 50 % sur `#171717` (5,26:1) est repris de `design-system.md` et non remesuré ici, comme le blanc `#FFFFFF` du bouton « Terminé » sur `#171717` ;
  - noms et rôles UIA, Narrateur, contraste élevé Windows, zoom 200 %, RTL, DPI 144 et 192.

**Verdict : Block.** Un finding HIGH reste ouvert : le menu natif n'a pas d'UIA alors que les dossiers en dépendent. C'est un préalable d'implémentation et non un défaut du storyboard. L'approbation ne couvrirait de toute façon que l'examen visuel et documentaire listé ci-dessus.

## Limites

- Storyboard statique : aucune interaction n'a été prototypée ni jouée.
- Les sources Dia décrivent surtout la version Mac. Dia et Arc n'ont été ni installés ni observés.
- Les états de survol des favoris, les suggestions de l'adresse et la ligne « Barre de favoris » des Réglages ne sont pas dessinés.
- La scène 12 réunit deux cas indépendants pour la lecture ; ils ne surviennent pas ensemble.
