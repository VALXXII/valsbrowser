# browser - Windows natif

## Preview favoris

Build portable : `build/bookmarks-preview/browser.exe`. Le profil, les réglages et les favoris de cette preview restent dans ce dossier, séparés du navigateur installé. Le ZIP `build/browser-bookmarks-preview.zip` contient le programme, ses ressources et les instructions ; extraire le dossier entier avant de lancer `browser.exe`.

Ctrl+D ou l'étoile enregistrent la page et ouvrent le popover de modification. Ctrl+Maj+B affiche/masque la barre ; Ctrl+Maj+O ouvre le tiroir Favoris avec recherche. Les dossiers, sous-dossiers, déplacements, duplication et suppressions se gèrent aussi au clic droit. Ctrl+Z ou le toast annulent la dernière opération. La barre se réordonne par glisser ou Ctrl+Maj+Gauche/Droite ; Alt+Haut/Bas réordonne les lignes du tiroir.

Les adresses invalides restent rouges jusqu'à correction ; le curseur utilise l'accent centralisé. Le futur choix de couleur dispose déjà du réglage persistant `accent=#RRGGBB` (pas encore de sélecteur dans les réglages). La visibilité persistante utilise `bookmarks-bar=auto|always|never`.

Vérifications : `test-bookmarks.ps1`, `test-bookmarks-ui.ps1`, `test.ps1`, `test-ui.ps1`, `test-design-system.ps1`, `test-accessibility.ps1`. Captures natives dans `build/bookmarks-ui-qa` ; compte rendu dans `design/bookmarks-preview-validation.md`.

Version int?gr?e sur `main`, issue de `feat/design-system-polish`. La branche historique `master` est conserv?e.

## Version Lottie + polish

La recherche Windows **browser** utilise d?sormais **`build/v2/browser-display-paced.exe`**, avec le profil existant `build/v2/profile`. Fermer puis rouvrir le navigateur pour charger cette version. Le paquet autonome **`build/display-pacing/browser.exe`** est ?galement disponible ; son profil serait distinct dans `build/lottie-alignment/profile`. Les deux dossiers contiennent la DLL officielle, les fontes, les ic?nes, les animations et les licences.

- Onglets : gap visible de 6 DIP, comme les boutons ; largeur 128, pas 134.
- Croix d'onglet : cible 24 x 24, inset egal de 4 DIP, glyphe Phosphor Bold 16.
- Focus : bordure blanche de 1 DIP ; opacite 10 % apres un clic souris (choix utilisateur), 50 % en navigation clavier (Tab, fleches, raccourcis) pour un contraste >= 3:1.
- Fleur vectorielle 16 DIP ? 32 % d?opacit?, centr?e en (22,22), align?e sur le bouton pr?c?dent et le favicon. Le premier onglet commence ? x44, comme le bouton suivant. La fleur ouvre toujours la liste des onglets.
- Actualiser et copier : vraies animations Lottie vectorielles, moteur natif ThorVG 1.1.3 statique. Aucun moteur JavaScript ni WebView d'interface ajoute.
- Copier : morph lien ? coche en 300 ms apr?s r?ussite du presse-papiers, maintien 1500 ms, retour en 300 ms. Actualiser : un seul tour antihoraire en 500 ms, sans modification de la longueur du trac?, sans boucle ni red?marrage pendant le tour. La dur?e du chargement ne change pas cette animation.
- Mode mouvement reduit : repli statique/fondu ; aucun timer d'animation permanent au repos.

## Compiler et verifier

Preparation initiale des outils locaux : `setup.ps1` (w64devkit et SDK WebView2). Puis :

```powershell
python build-lottie.py
.\build.ps1 -OutputDirectory build\lottie-alignment
python test-lottie.py
```

ThorVG et ses outils de build sont telecharges depuis leurs sources officielles, versions et SHA-256 figes. Aucun installateur systeme, changement du PATH global ou politique Windows. Si la politique locale interdit les scripts PowerShell, le build isole fourni est deja pret ; les scripts Python de tests invoquent directement le compilateur local.

Le moteur configure charge les animations locales autonomes du projet (shapes/paths) ; expressions et ressources externes image/font ne sont pas actives. Les deux assets sont certifies ici, sans promesse de compatibilite avec tous les effets Lottie.

## Validation et limites

Compilation isolee et cinq fixtures natives reussies : rendu de vrais frames JSON, alpha, DPI, cache borne, lifecycle, arret au repos, declenchement de copie apres succes, geometrie et regressions UI/favicon. Aucun lancement du navigateur ou chargement WebView2 durant ces verifications. Voir [validation finale](design/release-validation.md), [rapport natif initial](design/lottie-integration-validation.md) et [contrat des assets](design/lottie-spec.md).

L'ancien echec `Smoke download dialog did not open : 0x80004005` disparait avec le panneau natif ci-dessous : `test.ps1` passe de bout en bout (dont un vrai telechargement hors ligne). Le pacing du timer et la navigation complete restent a verifier.

## Paper

[Design system](https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-2-0) : synchronisation partielle. Les tokens et la marque ont ete mis a jour avant que le quota MCP hebdomadaire bloque les composants et timelines restants. Voir [reprise precise](design/paper-sync-pending.md). Les anciens canvas utilisateur sont conserves.

## Progression dans la barre URL

Fill blanc additionnel ?8%, sur toute la hauteur de la barre. Progression indicative par ?tapes de navigation, arriv?e ? pleine largeur puis fondu. Aucun timer pendant l?attente r?seau ou au repos. Voir [d?tails et validation](design/url-loading.md). Les six fixtures natives passent ; navigation r?elle non test?e sous le maintien de l?arr?t WebView2.

## Cadencement final valide

La preview native a ete approuvee. Le build installe utilise le compositor clock a la demande, avec cache de texte et invalidation limitee a la barre URL quand elle seule bouge. Zero reveil de frame au repos/minimise dans les tests. Cadence verifiee dans le harness natif a120Hz;240Hz et navigation WebView2 non mesures. Les anciennes estimations CPU sont retirees. Details: [display-pacing.md](design/display-pacing.md). Le raccourci browser ouvre build/v2/browser-display-paced.exe avec le profil existant.

## Restauration des onglets

Ctrl+Maj+T rouvre le dernier onglet fermé à sa position initiale (25 entrées, hors accueil). L’action figure aussi dans le menu principal et le menu contextuel des onglets.

À la fermeture de la fenêtre, les URL, les onglets vides et l’index actif sont sauvegardés dans `session`, à côté de `profile/`, puis restaurés au lancement. Un fichier absent ou invalide ouvre un nouvel onglet. Le smoke test utilise une session vierge sans toucher à ce fichier. `tests/session_test.cpp` couvre le format et les fichiers invalides ; `test.ps1` l’exécute avant le smoke test.

## Lecteurs d'écran

La barre du haut est dessinée en Direct2D ; `src/accessibility.h` la décrit aux lecteurs d'écran (Narrateur, NVDA, JAWS) via UI Automation. Chaque contrôle a un nom parlé, son raccourci à part (AcceleratorKey) et son état :

- « Liste des onglets », puis la liste « Onglets » : un élément par onglet (titre, « sélectionné », « 2 sur 3 », « Chargement en cours »), avec son bouton « Fermer l'onglet … ». Les onglets masqués par le défilement restent listés et sélectionnables.
- « Nouvel onglet », « Réduire », « Agrandir » / « Restaurer », « Fermer la fenêtre ».
- Barre d'outils « Navigation » : « Précédent » et « Suivant » (désactivés sans historique), « Adresse ou recherche » (valeur = URL ; une fois ouverte, c'est l'EDIT natif, nommé pareil), « Actualiser » / « Arrêter le chargement », « Copier le lien », « Historique » (Ctrl+H), « Téléchargements », « Navigation privée », « Réglages ».
- Tab, flèches et F6 annoncent l'élément focalisé ; changer d'onglet annonce la sélection ; une copie réussie dit « Lien copié ». Les lecteurs peuvent aussi activer, sélectionner et fermer directement.

`uiautomationcore.dll` est chargé à l'exécution : aucune dépendance de lien en plus, et le navigateur démarre aussi sur les Windows sans notifications UIA (avant Windows 10 1709). Vérification : `test-accessibility.ps1` (vrai client UI Automation, également dans `run_all.py`) ; à la main, Narrateur (Ctrl+Win+Entrée) puis Tab dans la barre du haut, ou Accessibility Insights for Windows.

## Historique des téléchargements

Le bouton Téléchargements ouvre un popover natif « Download history » sous la barre d'outils, d'après la frame Paper : 268 DIP de large, en-tête 24, lignes de 48 (tuile 32 avec l'icône Windows du fichier, nom 16 medium, détail 14). Le flyout WebView2 par défaut est remplacé (`DownloadStarting` marqué `Handled`) et le popover s'ouvre au début de chaque téléchargement.

- En cours : `↓ 50.4/200MB ⏱ 00:14` ; au survol, pause et annuler. En pause : reprendre / annuler. Terminé : `✓ In …/Downloads` ; au survol, afficher dans l'Explorateur (fichier sélectionné) et ouvrir. Échec, annulé ou fichier supprimé : statut explicite, réessayer si le moteur le permet, retirer de l'historique.
- Survol : le masque du nom puis les deux boutons glissent depuis la droite (24 / 12 DIP) en se défloutant (flou gaussien Direct2D, 4 → 0 DIP) et en fondu, décalés de 35 ms, 240 ms en cubic-bezier(.2,0,0,1) ; la sortie est groupée et plus courte (140 ms). Interruptible sans saut. Mouvement réduit : fondu 100 ms seul. Ouverture : contenu qui descend de 6 DIP en 180 ms. Images cadencées par le même compositor clock que la barre d'outils, aucun timer au repos (sauf le compte à rebours 1 s pendant un téléchargement visible).
- Clavier : Tab / flèches entre les boutons (la ligne focalisée se révèle), Entrée / Espace, Échap ferme et rend le focus à la page. Infobulles sur chaque bouton.
- Le popover montre les 4 derniers téléchargements et un lien « View all » (16 DIP sous la dernière ligne, hauteur 276).
- « View all » ouvre un drawer de 268 DIP ancré au bord droit, sous la barre d'outils jusqu'en bas de la fenêtre : tout l'historique groupé par Today / Yesterday / Last week / Last month / Last year (jours calendaires locaux). Chaque section se replie avec son chevron (aligné sur la croix) : les lignes se rangent sous l'en-tête en fondu, le chevron tourne de -90°, la suite remonte, 280 ms. Depuis « View all », le popover se transforme en drawer (morph 360 ms, cubic-bezier(.32,.72,0,1)) : son rectangle s'étire jusqu'au bord droit et au bas de la fenêtre pendant que la liste glisse d'un bloc depuis les lignes du popover, les en-têtes de section sortant de sous le titre et le reste apparaissant en fondu ; « View all » s'efface dans les premiers 30 %. Coût : aucune fenêtre redimensionnée ni transparente, une image Direct2D et une région GDI par image d'écran pendant 360 ms, rien au repos. La croix, Échap ou le bouton Téléchargements le font repartir vers la droite (220 ms) : la fenêtre se déplace sous une région qui épouse le coin arrondi de la fenêtre principale, sans repeindre. Un drawer ouvert autrement glisse depuis le bord droit (320 ms). Défilement molette lissé (160 ms), fondus de bord (16 DIP en haut, 128 DIP en bas) seulement quand du contenu dépasse. Mouvement réduit : tout est instantané.
- Les téléchargements terminés, échoués ou annulés (50 max) sont conservés dans `downloads`, à côté de `session`, avec leur date de début (format 2 ; une ligne sans date, héritée de l'ancien format, prend la date de création de son fichier, sinon celle du fichier d'historique, puis est réenregistrée).

Vérification : `test-downloads.ps1` (formatage, historique, timing du reveal, rendus PNG des états et des frames d'animation dans `build/downloads-qa`) et l'étape téléchargement du smoke `test.ps1`.

## Historique de navigation

Le bouton Historique de la barre d'outils (horloge Phosphor Bold « clock-counter-clockwise », juste à gauche de Téléchargements ; le champ d'adresse raccourcit de 30 DIP pour lui) ou Ctrl+H ouvre un drawer natif « History » ancré au bord droit sous la barre d'outils, sous son bouton et là où se rangent les drawers Téléchargements et Réglages (ouvrir l'un ferme les autres) : le jumeau du drawer des téléchargements (268 DIP, en-tête 24 avec la croix, sections Today / Yesterday / Last week / Last month / Last year repliables sur leur chevron, lignes de 48 DIP, fondus de bord au défilement, molette lissée). Les pages viennent de l'historique déjà tenu pour les suggestions d'adresse (`history`, à côté de `session`, 1 000 pages, une entrée par page datée de sa dernière visite) : rien de nouveau n'est stocké.

- Ligne : tuile 32 avec le favicon mémorisé du site (globe sinon, document pour un fichier local), titre de la page (ou son adresse lisible, ou le nom du fichier), détail `github.com · 14:05` (aujourd'hui et hier), `Nov 3` (cette année), `Nov 3, 2025` (avant).
- Clic : ouvre la page dans l'onglet actif et ferme le drawer. Au survol, le titre s'éclaire, le masque puis deux actions glissent depuis la droite en se défloutant (même révélation que les téléchargements) : « + » ouvre dans un nouvel onglet (aussi Ctrl+clic, clic milieu, Ctrl/Maj+Entrée), « × » retire la page de l'historique (et son favicon si le site n'a plus de page).
- Recherche : champ sous l'en-tête, focalisé à l'ouverture ; taper n'importe où filtre aussitôt (chaque mot dans le titre ou l'adresse, sans casse), bouton d'effacement, Entrée ouvre le premier résultat. États vides : « No history yet » / « No results for “…” ».
- Clavier : Tab / flèches haut-bas entre croix, recherche, sections et lignes ; droite / gauche atteignent les deux actions d'une ligne ; Entrée / Espace activent ; Suppr retire la ligne ; Échap efface d'abord la recherche, puis ferme et rend le focus à la page. Infobulles sur chaque bouton. Le bouton est nommé « Historique » (raccourci Ctrl+H) dans la barre « Navigation » pour UI Automation ; le Narrateur annonce l'ouverture (nombre de pages), l'élément focalisé, le nombre de résultats et les suppressions (notifications portées par ce bouton).
- Mouvement : glisse depuis le bord droit et repart par le même chemin (ressorts critiquement amortis des drawers, réponse 0,32 s à l'entrée, 0,24 s à la sortie), sans redimensionner ni repeindre pendant le glissement ; mouvement réduit : tout est instantané, révélations en fondu 100 ms. Aucun timer au repos (curseur de recherche fixe).

Vérification : `test-history.ps1` (filtrage, ordre, périodes, libellés de date, géométrie, persistance, rendus PNG des états dans `build/history-qa`) et l'étape historique du smoke `test.ps1` (ouverture depuis le bouton Historique, recherche, Échap, fermeture).

## Réglages

Le bouton Réglages (engrenage) ouvre un drawer natif de 332 DIP ancré au bord droit sous la barre d'outils, d'après la frame Paper « Réglages — panneau » (`src/settings-panel.h`). Il fonctionne comme le drawer des téléchargements, sans morph : il glisse depuis le bord droit (ressort critique, 320 ms) et repart par le même chemin (220 ms), la fenêtre se déplaçant sous une région qui épouse le coin de la fenêtre principale. Les deux drawers partagent le bord droit : en ouvrir un ferme l'autre. Un clic hors du drawer, Échap, la croix ou l'engrenage le referment ; les dialogues qu'il ouvre (dossier, confirmation) le laissent ouvert.

- Moteur de recherche : DuckDuckGo (par défaut), Google, Brave Search, puis Bing, Ecosia et Qwant derrière « Voir plus… » (dépliage comme une section de téléchargements ; déplié d'office si le moteur choisi s'y trouve). La sélection est une plaque qui glisse vers la ligne choisie avec sa coche (ressort 260 ms). La barre d'adresse cherche avec ce moteur. Chaque moteur montre son favicon, embarqué dans `assets/engines` (aucune requête réseau), atténué tant qu'il n'est pas choisi.
- Démarrage : « Restaurer les onglets » (activé) ; désactivé, le navigateur démarre sur un onglet vide (la session reste enregistrée).
- Téléchargements : « Enregistrer dans » (sélecteur de dossier Windows ; vide = dossier Téléchargements du système, transmis au profil WebView2) et « Demander où enregistrer » (boîte Enregistrer sous pour chaque téléchargement ; Annuler annule le téléchargement sans ligne d'historique).
- Moteur : « Suggestions de recherche » (désactivé, la liste sous la barre d'adresse ne s'ouvre plus ; la complétion en ligne reste).
- Confidentialité : « Anti-pistage » (Désactivé, Basique, Équilibré par défaut, Strict ; prévention du pistage du profil WebView2), « Mots de passe » et « Saisie automatique » (lignes à description, appliquées à chaque onglet), « Effacer en quittant » (à la fermeture : mêmes données que « Effacer les données… », sans confirmation, au plus 3 s d'attente), « Autorisations des sites ».
- Autorisations des sites : sous-vue poussée depuis la droite (la vue d'origine recule de 30 % et s'assombrit ; Échap, Retour arrière ou le bouton Précédent de la souris reviennent). La liste des sites vient des autorisations du profil ; un site ouvre son détail, où chaque autorisation (caméra, micro, position, notifications, presse-papiers, autres) se règle sur Autoriser, Demander ou Bloquer. « Tout retirer » et « Retirer les autorisations » remettent les sites à « Demander ». Sans aucun site, la ligne est inerte et l'explique.
- Performances : « Onglets en veille » (Jamais, après 30 min, 2 h par défaut, 6 h) : un onglet en arrière-plan depuis ce délai est suspendu (sauf s'il joue du son ou pendant un téléchargement), estompé dans la barre d'onglets et réveillé à la sélection.
- Affichage : « Zoom des pages » (80 à 150 %, appliqué à tous les onglets) et « Réduire les animations », qui s'ajoute à l'interrupteur « Effets d'animation » de Windows et s'applique immédiatement à toute l'interface.
- Pied : « Effacer les données… » (confirmation, puis historique, cookies, cache et données des sites du profil, onglets fermés et liste des téléchargements terminés ; les fichiers restent) et la version avec l'état des mises à jour ; le bouton lance une vérification, son icône tourne pendant la recherche.
- Les réglages à valeurs ouvrent le menu natif sous leur ligne, avec une description par choix ; les descriptions (panneau et menus) sont équilibrées sur leurs lignes, comme `text-wrap: balance`. Les menus entrent en 180 ms : fondu, glissement de 6 DIP depuis leur ancre et flou de 3 DIP qui se résorbe (instantané en mouvement réduit).
- Interrupteurs : la pastille glisse sur un ressort court (200 ms) et s'étire de 3 DIP pendant l'appui. Survol et appui teintent la ligne entière (seul fond : sur la ligne du dossier, l'icône s'éclaircit sans fond propre), focus clavier : Tab / flèches, Entrée / Espace. Défilement molette et fondus de bord quand la fenêtre est trop basse. Mouvement réduit : changements d'état instantanés, sans déplacement.
- Les réglages sont enregistrés dans `settings` (UTF-8, `clé=valeur`), à côté de `session` ; un fichier absent ou abîmé garde les valeurs par défaut. Le smoke test n'en lit ni n'en écrit aucun.

Vérification : `tests/settings_test.cpp` (format, valeurs par défaut, URL de recherche), lancé par `test.ps1`, et l'étape « settings drawer opened and closed » du smoke.

Imprimer : « Imprimer… » (Ctrl+P, aperçu d'impression du moteur) et « Enregistrer en PDF » (dans le dossier de téléchargements, ajouté à l'historique) sont dans le menu contextuel des onglets et des pages. Les menus du moteur sont en français (`--lang=fr`).

La croix de fermeture de la fenêtre se teinte au survol (#FA6A59 à 30 %, glyphe #FFD0C8 à 80 %), comme dans Paper.

## Mises à jour automatiques

Chaque merge poussé sur `main` du dépôt distant (privé) `VALXXII/browser` déclenche le workflow `.github/workflows/update-release.yml` : build Windows x64 avec les outils figés du projet, tests natifs, paquet `browser-0.3.N-win-x64.lsrupd` + `update-manifest-win-x64.txt`, puis publication dans le dépôt public de binaires `VALXXII/browser-releases`, seulement si tout a réussi et après activation (secret `UPDATES_RELEASE_TOKEN`, variable `UPDATES_PUBLISH_ENABLED=true`). Un merge local non poussé ne déclenche rien ; la première release automatique suppose ce workflow présent sur `main` distant. Aucune release n'est publiée à ce jour.

Le navigateur vérifie une fois au démarrage (silencieux hors ligne ou sans release), propose « Download », progression réelle, « Cancel », puis « Restart and install » ; rien n'est installé sans ce clic. Vérification manuelle : menu principal ▸ « Rechercher des mises à jour ». Installation par utilisateur dans `%LOCALAPPDATA%\browser` (versions séparées, profil et session stables dans `data\`, copie unique depuis un build portable, jamais de déplacement ni de suppression de l'ancien profil).

Build et tests sans PowerShell :

```
python tools/release/build.py --out build/update-integration
python tests/update-integration/run_all.py --app build/update-integration
python tools/release/package.py --app build/update-integration --out build/update-integration-dist
```

Version : `0.3.<git rev-list --count HEAD>`. Détails : [design/updates.md](design/updates.md).
