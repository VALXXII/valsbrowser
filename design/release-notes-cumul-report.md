# Carte « Nouveautés » : rattrapage et notes cumulées

Branche `VALXXII/release-notes-cumul` (depuis main `5056b8b7`, la sortie 0.3.898). Demande de Val, 25/11.

## 1. Ce qui change

**Avant :** `release-notes/notes.json` ne gardait que les notes de la dernière sortie. Quelqu'un qui sautait une version ne voyait jamais ses notes.
Les notes de 0.3.842 avaient été remplacées par celles de 0.3.898.

**Maintenant :** la carte montre les notes de **toutes les versions pas encore vues**, la plus récente d'abord. Une même ligne n'y apparaît qu'une fois.

- **Les notes par version viennent de l'historique git** de `notes.json` sur main. Chaque fusion qui a changé le fichier donne une version avec ses notes, sous le numéro de cette fusion.
  Sur main aujourd'hui, l'historique retrouve exactement les deux sorties qui avaient des notes : 0.3.842 (4 + 4 lignes) et 0.3.898 (3 + 0).
  Personne n'a rien à recopier : la procédure de sortie ne change pas (remplacer `notes.json` par les notes de la sortie).
- **Le navigateur retient « vu jusqu'à la version X »** (`seen=0.3.950` dans le fichier `release-notes` de ses données). Après une mise à jour, il montre les versions plus récentes que X, jusqu'à la version installée.
  Les anciens états restent compris :
  - l'identifiant fixe des versions jusqu'à 0.3.840 vaut « vu jusqu'à 0.3.646 » ;
  - l'empreinte écrite par les versions 0.3.842 à 0.3.898 vaut la version de ces notes.
- Pas d'état : ce qui est plus récent que la version installée avant la mise à jour compte comme nouveau. Première installation : pas de carte, la version est retenue.
- Aucune note pas encore vue : pas de carte, la notification « Mise à jour installée ».
- **Affichage provisoire :** 4 lignes par section au plus, comme aujourd'hui. Ce sont les premières lignes de la version la plus récente, puis celles des versions plus anciennes.
  Le fichier peut contenir jusqu'à 10 lignes par section, pour qu'un rattrapage soit complet le jour où la carte saura en montrer plus (§ 3).
- « À propos › Notes de version » ouvre les notes les plus récentes, avec la même limite.

## 2. Le texte de la carte de rattrapage (sortie de 19 h 30)

Pour quelqu'un qui n'a rien vu depuis 0.3.646, ce qui est le cas de Val, la carte montre les 4 premières lignes de chaque section.
Les autres lignes sont gardées dans le fichier pour la suite.

**Ajouts** (affichées : 1 à 4)

| # | Français | English |
| --- | --- | --- |
| 1 | Mode Focus : la page prend toute la place, les onglets se cachent. | Focus mode: the page fills the window, the tabs step aside. |
| 2 | Mode Annotation : un tutoriel, la bulle Commentaire ou le chat en panneau. | Annotation mode: a tutorial, the Comment bubble or the chat panel. |
| 3 | Onglets sur le côté : en panneau ou en colonne compacte. | Tabs on the side: as a panel or a compact column. |
| 4 | Nouvelles fenêtres : impression avec aperçu, pages d’erreur, onglet planté. | New windows: print with preview, error pages, a crashed tab. |
| 5 | Mots de passe et infos enregistrées proposés sous les champs. | Passwords and saved info suggested under form fields. |
| 6 | Pastille « Local » pour les projets ouverts depuis cet ordinateur. | A “Local” pill for projects opened from this computer. |
| 7 | Menu des modèles : Claude et Codex réunis, avec leurs vrais modèles. | Model menu: Claude and Codex together, with their real models. |
| 8 | Partage d’écran : un site peut être bloqué une fois pour toutes. | Screen sharing: a site can be blocked for good. |
| 9 | Emoji en couleur dans les favoris, les onglets et les menus. | Colour emoji in bookmarks, tabs and menus. |
| 10 | La fenêtre s’anime quand vous la réduisez ou l’agrandissez. | The window animates when you minimize or maximize it. |

**Corrections** (affichées : 1 à 4)

| # | Français | English |
| --- | --- | --- |
| 1 | Un téléchargement en attente se garde ou se supprime en un clic. | A download waiting for approval can be kept or deleted in one click. |
| 2 | La bulle d’annotation suit son élément et n’est plus coupée. | The annotation bubble follows its element and is no longer cut off. |
| 3 | Historique instantané, même avec des milliers de pages. | Instant history, even with thousands of pages. |
| 4 | Plus d’éclair sur la page quand un panneau s’ouvre. | No more flash over the page when a panel opens. |
| 5 | Les favoris importés retrouvent leurs icônes. | Imported bookmarks get their icons. |
| 6 | Les vérifications anti-robot de certains sites passent à nouveau. | Some sites’ anti-bot checks pass again. |
| 7 | Premier lancement sans blocage, onglets rouverts après une mise à jour. | No freeze on first launch, tabs reopen after an update. |
| 8 | La fenêtre se redimensionne par ses bords, même avec le chat ouvert. | The window resizes from its edges, even with the chat open. |
| 9 | Le chat trouve le bon dossier de votre projet. | The chat finds your project’s folder. |
| 10 | L’agent demande toujours avant d’agir. | The agent always asks before acting. |

Sources : les sorties de 0.3.648 à 0.3.898 (`gh release list -R VALXXII/browser-releases`) et les PR groupées #104 à #148, sans les points internes (tests, copie publique, CI, site).
« Navigateur par défaut » n'y est pas : il était déjà dans 0.3.646.

**Si « Ton agent » sort à 19 h 30** : ajoutez cette ligne en 2e position des « ajouts », retirez la 10e (la fenêtre qui s'anime), et faites-le dans la même fusion que cette branche.
- FR : « Ton agent : installez et connectez Claude ou Codex depuis le navigateur. »
- EN : « Your agent: install and sign in to Claude or Codex from the browser. »

**À fusionner dans la sortie de 19 h 30 elle-même**, pas avant : chaque modification de `notes.json` sur main compte comme une version.

## 3. Proposition pour Designer (question de design, rien n'est dessiné ici)

**Ce que dit Paper aujourd'hui** (page « Notes de version — modale », planches `142E-0` Mini et `144O-0` Full, lues sans rien modifier) :
- la Mini fait 343 × 218 : le haut des notes, un fondu, puis « Voir tout » ;
- la vue complète a une hauteur au contenu, sans hauteur maximale ni zone de défilement ;
- deux sections, « Ajouts » puis « Corrections », avec 2 et 3 lignes. Rien ne parle de versions ni de regroupement.

**Le besoin :** un rattrapage compte 20 lignes (10 + 10). Avec la mise en page actuelle, la vue complète ferait environ 900 DIP, plus haut que la plupart des fenêtres.
Aujourd'hui, le code coupe à 4 lignes par section : la carte complète fait environ 550 DIP.

**Questions pour Designer :**
1. **Longueur.** Que fait la vue complète quand il y a beaucoup de lignes ?
   - A. Une hauteur maximale (par exemple la hauteur de la Mini × 2, ou une part de la fenêtre), avec le corps qui défile sous l'en-tête fixe. Faut-il un fondu en bas ?
   - B. Toujours 4 lignes par section dans la carte, et un lien « Toutes les nouveautés » qui ouvre une liste plus longue (dans quoi : la même carte agrandie, un tiroir, une page ?).
2. **Regroupement.** Pour un rattrapage, on garde les deux sections « Ajouts » et « Corrections », avec des lignes déjà regroupées par thème dans leur texte (ce qui est fait aujourd'hui) ? Ou bien des intertitres par thème (Mode Annotation, Onglets, Fenêtres…) ? Ou bien par version ou par date ?
3. **Portée.** Faut-il dire ce que couvre la carte, par exemple un sous-titre « Depuis votre dernière mise à jour », une date, ou le nombre de nouveautés ? Les planches `12VC-0` / `12WI-0` avaient un sous-titre « Prochaine mise à jour » : il n'est pas implémenté.
4. **Mini.** Montre-t-elle toujours seulement le haut, avec « Voir tout » ? Faut-il un compte, par exemple « Voir les 20 nouveautés » ?
5. **Limites.** 10 lignes par section dans le fichier et 80 caractères par ligne : est-ce que ça va ?

Dès que Designer répond, l'affichage change à un seul endroit (`ReleaseNotes::DisplayLines` et la vue complète de `src/modal.h`). Les données sont déjà complètes.

## 4. Vérifications (sans écran)

- `python -I tests/update-integration/test_release_tools.py` : tous les tests passent, dont 9 pour les notes.
  - Un vrai petit dépôt git : commits directs, fusion `--no-ff` (le numéro est celui de la fusion), version sans notes, anciennes notes invalides ignorées, notes pas encore commitées.
  - La sélection cumulée.
  - Main à 0.3.898 donne bien 0.3.898 et 0.3.842.
- `test-release-notes.ps1`, sur un bureau Windows caché : 263 vérifications, 3 échecs. Ce sont les 3 vérifications du focus rendu à la fenêtre propriétaire, qui demandent le premier plan : main donne les mêmes 3 échecs sur un bureau caché.
  Toutes les nouvelles vérifications passent :
  - installation neuve ;
  - mise à jour qui saute plusieurs versions ;
  - versions sans notes ;
  - déjà vu ;
  - réparation ;
  - ancien identifiant (le cas de Val) ;
  - empreinte de 0.3.842–0.3.898 ;
  - état absent, inconnu, abîmé ou futur ;
  - copie portable ;
  - version de développement.
  Les 28 lignes de toutes les versions tiennent sur deux lignes de la carte, en français et en anglais. Les rendus sont dans `build/release-notes/release-{full,mini}-{fr,en}-144.png`.
- `python -I tools/i18n/check_strings.py` : 0 constat. Build complet (`tools/release/build.py`) : voir le compte rendu de la branche.
- `tests/e2e/release_notes_e2e.py` (vrai navigateur) : mis à jour, la carte attendue est calculée depuis l'historique. Il demande de vraies fenêtres : à lancer pendant la tournée.
