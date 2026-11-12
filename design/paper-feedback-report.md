# Retours Paper du 12 novembre 2026 : scrollbar, Menu V2, stockage persistant, mesures en mode Design

## Contexte

Quatre fils de commentaires ouverts par Val.S dans le fichier Paper « Browser Design ». Ce
rapport donne, pour chaque fil, la cause, ce qui a été corrigé dans le code et dans Paper, et
les preuves.

- **Base :** `origin/main` 4a3971d, release v0.3.235 publiée dans `VALXXII/browser-releases`.
- **Branche :** `VALXXII/paper-feedback-fixes`.
- **Statut de fusion :** rien n'est fusionné. Le coordinateur décide de la fusion, de la
  publication et de la résolution des fils.
- **Opérations Paper :** le détail est dans
  [paper-feedback/ledger.json](paper-feedback/ledger.json).
- **Preuves :** sorties de test dans `paper-feedback/proofs/`, en `.txt` et premières
  exécutions comprises.

## Résumé par fil

| Fil | Demande | Correction | État |
| --- | --- | --- | --- |
| c-l6gvbn (S16) | Poignée de scrollbar visible sur un site sombre, « créer et merge » | Code et variante Paper D | Prêt, à fusionner. Le fil se résout après adoption dans main. |
| c-bu5dzt (Menu V2) | « J'ai clean les icônes pour éviter le clipping des opacités si tu peux fix » | Code (rendu natif) | Prêt, à fusionner |
| c-qdlkgz (SYJ) | « Garder les données » se lit comme « garder les identifiants » | Copie dans le code, les tests et DQ5 | Prêt, à fusionner |
| c-wsk4r6 (B1) | Voir paddings et gaps en mode Design | Démonstration Paper sur VQC-0 uniquement, exception explicite | **Résolu par root** après son contrôle visuel. C'était du design seul : aucune implémentation MCP n'est promise. |

## 1. Scrollbar sur site sombre (c-l6gvbn)

**Cause.** `src/viewport-scrollbar.h` dessinait toujours la poignée en #171717 à 56 % avec un
anneau blanc à 16 %. Sur un fond #121212, le contraste tombe sous 1,2:1 : la poignée est
pratiquement invisible. Le test le reproduit (« defect reproduced »).

**Correction.**

*Côté page.* Le script de page existant (document principal uniquement) envoie maintenant,
pour chaque piste, la couleur réellement peinte dessous. Il la calcule ainsi :
- `elementFromPoint` au bord droit pour la piste verticale, au bord bas pour la piste
  horizontale ;
- remontée des ancêtres et composition source-over de toutes les couleurs de fond RGBA
  (transparences imbriquées comprises) jusqu'au canevas ;
- canevas sombre (#121212) seulement si la page le déclare : `color-scheme: dark`, ou
  `light dark` alors que le système préfère le sombre, propriété CSS ou balise meta. Une page
  claire sous un Windows sombre reste donc claire, le thème Windows seul ne suffit jamais ;
- les couleurs sont normalisées par un canevas 1×1 (oklch, color(), etc.), avec un cache.

*Côté natif.* `Scrollbar::DarkSurface` choisit la poignée dont le remplissage au repos a le
meilleur contraste WCAG sur cette couleur. Sur fond sombre, c'est la poignée inversée :
remplissage #FFFFFF, anneau #171717. Forme, alphas (56/80 %, 16/24 %), animations, zone de
survol et glisser ne changent pas. Une surface inconnue (ancien message, ou rien de reçu)
garde la poignée validée.

*Nouvelle mesure.* Elle a lieu au défilement et au redimensionnement comme avant, et aussi :
- quand le pointeur approche de la piste ;
- quand la préférence `prefers-color-scheme` change ;
- quand les attributs `class`, `style`, `data-theme` ou `data-color-mode` de `html` ou `body`
  changent (un seul `MutationObserver`, limité aux attributs de ces deux nœuds).

Aucun minuteur au repos.

*Message.* `Scrollbar::ParseMessage` est strict : 10 champs (ancien script) ou 12 champs.
Les surfaces sont des entiers 0xRRGGBB ou -1. Tout le reste est rejeté : texte en trop,
valeurs non finies ou hors bornes. Ces métadonnées ne déclenchent aucune permission.

*Limites documentées.*
- Les images et dégradés de fond ne sont pas lus. On prend la couleur de fond déclarée sous
  l'image (cas testé).
- Le contenu d'une iframe d'une autre origine n'est pas lu : on compose les ancêtres de
  l'iframe. Aucun contournement d'origine.
- Seule la couleur de fond de chaque élément entre dans la composition : ni `opacity` ni
  `mix-blend-mode`.

**Paper.** Nouvelle planche « Scrollbar minimale D — site sombre » à droite de C, dans S16.
C'est un duplicata de A, avec un viewport #121212 et la poignée #FFFFFF8F / anneau #17171729.
L'en-tête S16 gagne la ligne D et une pastille « À fusionner · D site sombre ». L'ancre du fil
(cadre « Contenu » de l'en-tête) est conservée.

**Tests.**
- `tests/design-system/scrollbar_test.cpp` : 186 contrôles, dont la composition, le défaut
  reproduit, le choix par contraste sur toute la gamme de gris, un raster sombre de même
  couverture et l'analyse stricte 10/12/11/13 champs. Le premier run échouait sur un seuil
  4,5:1 mal choisi (le contrat clair donne 4,1:1 sur blanc). Le seuil passe à 3:1 (WCAG
  1.4.11) : voir `proofs/scrollbar_test-run1.txt` et `run2.txt`.
- `tests/e2e/scrollbar_surface_e2e.py` : test moteur. Le vrai `ScrollbarScript` tourne dans
  le runtime Chromium de WebView2 (EdgeCore 157, headless, profil jetable), piloté par
  DevTools : 17 cas sur 17, avec :
  - noir 50 % sur blanc et blanc 50 % sur noir ;
  - transparences imbriquées ;
  - coque d'application transparente ;
  - une couleur différente pour chaque piste ;
  - `color-scheme` dark, light dark, et la balise meta ;
  - page claire et page sans style sous un OS sombre ;
  - bascule de thème par classe et par style ;
  - oklch ;
  - image de fond.

  Historique des runs :
  - runs 1 à 3 (`--dump-dom`) : 2 attentes fausses. Le runtime suit le thème sombre de
    Windows, et `--dump-dom` capture la page avant les minuteurs. Le harnais est passé à
    DevTools et la préférence est émulée à chaque cas ;
  - run 4 : vert.
- Non couvert : un E2E dans browser.exe avec une vraie fenêtre (DPI réel, rendu de la fenêtre
  « layered »). Le raster est testé hors écran à 100/125/175 %. Mouvement réduit, glisser et
  clavier ne changent pas : mêmes chemins, tests existants verts.

## 2. Menu V2 : icônes de disposition (c-bu5dzt)

**Cause.** Dans `ContextMenu::DrawGlyph`, le contour de 1,5 DIP puis le demi-remplissage
étaient dessinés avec le même blanc translucide. Le remplissage recouvre le contour sur trois
côtés, donc l'alpha doublait sur cette bande : 197 contre 139 sur fond #171717, reproduit à
100, 125 et 150 %.

**Correction.** Je reporte le nettoyage de Val.S dans Paper (9Z2-0 : contour et remplissage
#8A8A8A opaque, même valeur sur la rangée survolée). Dans le code, une seule encre opaque
`glyphInk` 0x8A sert au contour et au remplissage, sans majoration au survol.
- Désactivé : 0x61, l'équivalent exact de l'ancien blanc 32 % sur #171717.
- La fonction est partagée par les 8 glyphes (droite, gauche, dessus, dessous, ligne,
  colonne). C'est la même cause, donc la même correction pour les 8, sans substitution
  d'icônes.
- La source Paper de Val.S n'a pas été touchée.

**Tests.** `tests/ui/menu_glyph_render_test.cpp`, rendu WIC hors écran, 37 contrôles :
- l'ancien rendu est reproduit ;
- contour seul = contour sous le remplissage = remplissage = 138, au repos comme sur la
  rangée survolée ;
- 8 glyphes × 3 DPI : aucun pixel au-dessus de l'encre, coins antialiasés compris ;
- le niveau désactivé est conservé.

Les rendus avant/après sont dans `paper-feedback/renders/`. Non-régression : suite split view
complète et `context_menu_hover_test` verts.

## 3. Stockage persistant, type 13 (c-qdlkgz)

**Cause.** La demande de x.com est bien le type 13, PERSISTENT_STORAGE. Ce n'est ni une
notification ni un enregistrement de mot de passe : le coffre reste désactivé. Mais l'ancienne
copie, « x.com veut conserver ses données sur cet appareil », se lisait comme « garder mes
identifiants ».

**Nouvelle copie** (code, tests, docs, Paper) :
- titre : « <site> veut protéger son stockage local » ;
- sous-titre : « Protège les données locales du site quand le navigateur libère de l'espace.
  Les mots de passe sont gérés séparément. »

Ce qu'elle dit et ne dit pas :
- elle décrit seulement la portée de l'autorisation : pas d'éviction quand le navigateur
  libère de l'espace ;
- elle ne promet pas que le site ne garde aucun élément de session ;
- elle ne garantit rien d'absolu : l'utilisateur peut toujours effacer.

Ce qui ne change pas :
- le nom de rangée « Stockage persistant » ;
- la case « Mémoriser pour ce site », comme dans le code ;
- pas de « Plus d'infos » : le contrat final reste sans aide repliable.

**Paper.** Dans la planche DQ5 « Par type », une 4e carte « Stockage persistant » (glyphe
base de données exporté du code). La rangée passe en retour à la ligne : la carte va à la
ligne, DQ5 grandit sans chevaucher S15. La planche SYJ (abandonnée, avec son fil) est
intacte.

**Tests.** Mis à jour et verts :
- `tests/modal_test.cpp` (via `test-popups.ps1 -SkipPixels` : 271 contrôles, 0 échec) ;
- `tests/elements_integration_test.cpp` ;
- l'attente du titre dans l'E2E `tests/e2e/permission_help_e2e.ps1`. Cet E2E n'a pas été
  relancé : il demande l'application complète avec WebView2.

Docs mises à jour : `design/permission-kinds.md` (justification) et
`design/origin-prompt-help.md`.

## 4. Paddings et gaps en mode Design (c-wsk4r6) : exception B1

**Autorisation.** L'utilisateur a répondu par l'intermédiaire du coordinateur : « Autoriser
uniquement ce feedback ». La portée a été approuvée avant toute modification : VQC-0 seulement.

**Cible.**
- L'ancre GNK-0 (dans GFT-0) n'a plus de parent : elle est obsolète.
- Son équivalent vivant est VQC-0 sur la page « MCP - Design UI UX ». Preuve : mêmes écarts
  relatifs entre les 5 planches B1 et même nombre de nœuds (100).
- Le décalage du commentaire (231/373 dans le contenu) pointe le bouton « Payer »
  sélectionné.

**Styles réels du spécimen.**
- Formulaire : colonne flex, gap 20.
- Bouton `button.btn-primary` : 240×44, **sans padding** (contenu centré).
- Les 24 px au-dessus du bouton sont l'échafaudage de la maquette (place de l'étiquette), pas
  un style du site.

**Modification.**
- Trois bandes « Mesure — gap 20 », en position absolue hors flux et sous les éléments du
  site, exactement sur les trois gaps de 20 px du parent flex de l'élément inspecté :
  - couleur : jeton existant `--color-browser-split-tint` (accent 16 %) ;
  - étiquette « 20 » en JetBrains Mono 12 sur pastille accent.
- L'étiquette de sélection devient « button.btn-primary · 240×44 · padding 0 ».
- Aucune bande de padding fictive.

**Vérification.**
- Le diff JSX et styles ne montre que les 9 nœuds ajoutés et le texte de l'étiquette (qui
  passe de 207 à 293 px de large, sans rien recouvrir).
- Paper a ajouté de lui-même `position: relative` sur le formulaire et ses trois enfants
  précédents. Une tentative de remise en `static` a été refusée par Paper. Sans décalage, ce
  style n'a aucun effet : les positions mesurées sont identiques (titre 0, e-mail 54, carte
  142, bouton 230). Voir `proofs/b1-exception-geometry.txt`.
- Paper ignore `pointer-events`, qui n'apparaît donc pas dans les styles calculés.
- Captures : `screens/VQC-0-before.jpg` et `screens/VQC-0-after.jpg`.

**Contrat pour une intégration future** (hors canvas, aucun code dans ce lot). En mode
inspecteur, pour l'élément sélectionné, le panneau afficherait :
- sa taille rendue ;
- son padding calculé (`getComputedStyle`) ;
- le gap de son parent flex ou grid, peint en bandes entre les enfants.

Ces valeurs viendraient du moteur au moment de l'inspection. Rien de cela n'existe
aujourd'hui, et rien n'est promis : le plan d'annotations MCP reste au stade de plan.

**Fil.** Résolu par root après son contrôle visuel de la capture actuelle (design seul). Les trois autres fils restent ouverts jusqu'à l'adoption du code.

## Protections et changements extérieurs

- **Icon :** identique à chaque vérification.
- **Jetons :** b5366ac2 inchangés.
- **Commentaires :** les 12 fils sont identiques (statut, ancre, messages, dates). Aucun n'a
  été résolu par ce lot.
- **Page « MCP - Design UI UX », hors VQC :** pas modifiée par ce lot. Deux changements
  extérieurs observés, sans attribution :
  - VVO-0, une planche de 2 nœuds apparue puis détachée pendant la session ;
  - VLO-0 : polices passées de `system-ui` à `var(--font-browser-inter)` sur une dizaine de
    textes (`proofs/external-change-VLO.txt`), visiblement un travail en cours.

  Rien n'a été restauré.
- **Référence « Browser — référence unifiée » :** 165 racines, aucun chevauchement. Les
  hauteurs « fit-content » ne sont pas mesurées quand la page est inactive. J'ai donc vérifié
  le bas de DQ5 par capture : environ y 29 430, avant S15 à 29 630.

## Fichiers

- **Code :** `src/viewport-scrollbar.h`, `src/viewport-scrollbar-host.h`,
  `src/context-menu.h`, `src/origin-prompt.h`.
- **Tests :** `tests/design-system/scrollbar_test.cpp`, `tests/e2e/scrollbar_surface_e2e.py`
  (nouveau), `tests/ui/menu_glyph_render_test.cpp` (nouveau), `tests/modal_test.cpp`,
  `tests/elements_integration_test.cpp`, `tests/e2e/permission_help_e2e.ps1`.
- **Build :** `tools/release/build.py` a été lancé vers `build/feedback-v2`. Résultat :
  « Build OK 0.3.235 ».

  La chaîne d'outils (`tools/w64devkit`), `deps/` et `build/lottie-backend/src` sont
  utilisés en lecture seule depuis le checkout parent, par des jonctions ignorées par git.

## Revue indépendante de la PR #68 (cc231a6) : deux réserves reproduites et corrigées

Le reviewer a travaillé au SHA cc231a6 épinglé. J'ai reproduit ses deux constats avec le script inchangé
(`proofs/scrollbar_surface_e2e-run6-repro-review.txt`, 7 cas en échec), puis je les ai corrigés dans
`surface()` et `darkScheme()` de `src/viewport-scrollbar-host.h`.

1. **Propagation du fond du body au canevas.**
   - Cas du reviewer : `body{background:#121212}`, marge par défaut de 8 px, `html` transparent, OS clair.
   - Défaut : la piste verticale tombe dans la marge, donc sur `html`, et le script annonçait #FFFFFF.
     Or le canevas est #121212, parce que le fond du body se propage au canevas (CSS Backgrounds 3,
     §2.11.2).
   - Correction :
     - le fond du canevas est celui de la racine ;
     - si la racine n'a ni couleur ni image de fond, c'est celui du body HTML, et ce body n'est alors plus
       compté dans la chaîne (pas de double composition d'un fond translucide) ;
     - une racine qui a son propre fond, même translucide ou seulement une image, reste le canevas, et le
       body ne peint que sa boîte.
2. **Les ancêtres ne peignent que dans leur propre boîte.**
   - Défaut trouvé en reproduisant le premier : un enfant qui déborde dans la marge faisait compter le
     fond du body hors de sa boîte.
   - Correction : chaque ancêtre est composé seulement si le point est dans son `getBoundingClientRect`.
3. **Précédence color-scheme CSS / meta.**
   - Défaut : le script concaténait la propriété CSS et la balise meta. Par exemple, CSS `light` + meta
     `dark` sous un OS sombre donnait #121212.
   - Correction : la valeur calculée `color-scheme` de la racine prime ; la balise meta ne compte que si
     le CSS vaut `normal`. Pour `light dark`, la préférence de l'OS départage.

**Tests ajoutés (moteur réel, `tests/e2e/scrollbar_surface_e2e.py`) :** 19 nouveaux cas.
- Les cas exacts du reviewer.
- La marge par défaut, avec un contenu seulement haut ou aussi large que la page.
- Un body translucide, avec et sans marge : pas de double composition.
- Un `html` avec un fond explicite, translucide, ou seulement une image.
- Les conflits CSS/meta, `only light`, et la balise meta `light dark` sous un OS clair ou sombre.

**Résultats :**
- Run 7 : 36 cas sur 36, les 17 anciens compris (`proofs/scrollbar_surface_e2e-run7-after-review-fix.txt`).
- Le build complet et `test-design-system.ps1` restent verts (`proofs/*after-review-fix.txt`).
- Aucune modification Paper n'était nécessaire pour ce défaut de script.
