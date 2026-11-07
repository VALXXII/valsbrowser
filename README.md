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
- Mode mouvement reduit : repli statique/fondu ; aucun timer d'animation permanent au repos. Telechargements inchanges, animation non raccordee.

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

Le diagnostic precedent `Smoke download dialog did not open : 0x80004005` reste en suspens. `test.ps1` est le smoke moteur suspendu ; son assertion reste inchangee. Le rendu en fenetre reelle, le pacing du timer et la navigation complete restent a verifier.

## Paper

[Design system](https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-2-0) : synchronisation partielle. Les tokens et la marque ont ete mis a jour avant que le quota MCP hebdomadaire bloque les composants et timelines restants. Voir [reprise precise](design/paper-sync-pending.md). Les anciens canvas utilisateur sont conserves.

## Progression dans la barre URL

Fill blanc additionnel ?8%, sur toute la hauteur de la barre. Progression indicative par ?tapes de navigation, arriv?e ? pleine largeur puis fondu. Aucun timer pendant l?attente r?seau ou au repos. Voir [d?tails et validation](design/url-loading.md). Les six fixtures natives passent ; navigation r?elle non test?e sous le maintien de l?arr?t WebView2.

## Cadencement final valide

La preview native a ete approuvee. Le build installe utilise le compositor clock a la demande, avec cache de texte et invalidation limitee a la barre URL quand elle seule bouge. Zero reveil de frame au repos/minimise dans les tests. Cadence verifiee dans le harness natif a120Hz;240Hz et navigation WebView2 non mesures. Les anciennes estimations CPU sont retirees. Details: [display-pacing.md](design/display-pacing.md). Le raccourci browser ouvre build/v2/browser-display-paced.exe avec le profil existant.

## Restauration des onglets

Ctrl+Maj+T rouvre le dernier onglet fermé à sa position initiale (25 entrées, hors accueil). L’action figure aussi dans le menu principal et le menu contextuel des onglets.

À la fermeture de la fenêtre, les URL, les onglets vides et l’index actif sont sauvegardés dans `session`, à côté de `profile/`, puis restaurés au lancement. Un fichier absent ou invalide ouvre un nouvel onglet. Le smoke test utilise une session vierge sans toucher à ce fichier. `tests/session_test.cpp` couvre le format et les fichiers invalides ; `test.ps1` l’exécute avant le smoke test.
