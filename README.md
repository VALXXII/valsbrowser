# browser - Windows natif

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

## Historique des téléchargements

Le bouton Téléchargements (et le menu Réglages) ouvre un popover natif « Download history » sous la barre d'outils, d'après la frame Paper : 268 DIP de large, en-tête 24, lignes de 48 (tuile 32 avec l'icône Windows du fichier, nom 16 medium, détail 14). Le flyout WebView2 par défaut est remplacé (`DownloadStarting` marqué `Handled`) et le popover s'ouvre au début de chaque téléchargement.

- En cours : `↓ 50.4/200MB ⏱ 00:14` ; au survol, pause et annuler. En pause : reprendre / annuler. Terminé : `✓ In …/Downloads` ; au survol, afficher dans l'Explorateur (fichier sélectionné) et ouvrir. Échec, annulé ou fichier supprimé : statut explicite, réessayer si le moteur le permet, retirer de l'historique.
- Survol : le masque du nom puis les deux boutons glissent depuis la droite (24 / 12 DIP) en se défloutant (flou gaussien Direct2D, 4 → 0 DIP) et en fondu, décalés de 35 ms, 240 ms en cubic-bezier(.2,0,0,1) ; la sortie est groupée et plus courte (140 ms). Interruptible sans saut. Mouvement réduit : fondu 100 ms seul. Ouverture : contenu qui descend de 6 DIP en 180 ms. Images cadencées par le même compositor clock que la barre d'outils, aucun timer au repos (sauf le compte à rebours 1 s pendant un téléchargement visible).
- Clavier : Tab / flèches entre les boutons (la ligne focalisée se révèle), Entrée / Espace, Échap ferme et rend le focus à la page. Infobulles sur chaque bouton.
- Le popover montre les 4 derniers téléchargements et un lien « View all » (16 DIP sous la dernière ligne, hauteur 276).
- « View all » ouvre un drawer de 268 DIP ancré au bord droit, sous la barre d'outils jusqu'en bas de la fenêtre : tout l'historique groupé par Today / Yesterday / Last week / Last month / Last year (jours calendaires locaux). Chaque section se replie avec son chevron (aligné sur la croix) : les lignes se rangent sous l'en-tête en fondu, le chevron tourne de -90°, la suite remonte, 280 ms. Depuis « View all », le popover se transforme en drawer (morph 360 ms, cubic-bezier(.32,.72,0,1)) : son rectangle s'étire jusqu'au bord droit et au bas de la fenêtre pendant que la liste glisse d'un bloc depuis les lignes du popover, les en-têtes de section sortant de sous le titre et le reste apparaissant en fondu ; « View all » s'efface dans les premiers 30 %. Coût : aucune fenêtre redimensionnée ni transparente, une image Direct2D et une région GDI par image d'écran pendant 360 ms, rien au repos. La croix, Échap ou le bouton Téléchargements le font repartir vers la droite (220 ms) : la fenêtre se déplace sous une région qui épouse le coin arrondi de la fenêtre principale, sans repeindre. Un drawer ouvert autrement glisse depuis le bord droit (320 ms). Défilement molette lissé (160 ms), fondus de bord (16 DIP en haut, 128 DIP en bas) seulement quand du contenu dépasse. Mouvement réduit : tout est instantané.
- Les téléchargements terminés, échoués ou annulés (50 max) sont conservés dans `downloads`, à côté de `session`, avec leur date de début (format 2 ; une ligne sans date, héritée de l'ancien format, prend la date de création de son fichier, sinon celle du fichier d'historique, puis est réenregistrée).

Vérification : `test-downloads.ps1` (formatage, historique, timing du reveal, rendus PNG des états et des frames d'animation dans `build/downloads-qa`) et l'étape téléchargement du smoke `test.ps1`.

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
