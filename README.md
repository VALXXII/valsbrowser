# browser ? Windows natif

Branche de travail : `feat/design-system-polish`. `master` et le raccourci habituel ne sont pas modifies par ce build.

## Version Lottie + polish

Lancer **`build/lottie-polish/browser.exe`** pour essayer le nouveau build. Son dossier contient la DLL WebView2 officielle, les fontes, icones, animations et licences. Le profil de cette version est `build/lottie-polish/profile`, distinct de celui de la version habituelle.

- Onglets : gap visible de 6 DIP, comme les boutons ; largeur 128, pas 134.
- Croix d'onglet : cible 24 x 24, inset egal de 4 DIP, glyphe Phosphor Bold 16.
- Focus clavier : bordure blanche de 1 DIP, opacite 10 %, selon le choix utilisateur.
- Fleur vectorielle 16 DIP en haut a gauche, ouvrant toujours la liste des onglets.
- Actualiser et copier : vraies animations Lottie vectorielles, moteur natif ThorVG 1.1.3 statique. Aucun moteur JavaScript ni WebView d'interface ajoute.
- Copier : lien vers coche apres reussite du presse-papiers, maintien 1500 ms, retour vers lien. Refresh termine son cycle a la fin de la navigation, avec plafond de 5 secondes.
- Mode mouvement reduit : repli statique/fondu ; aucun timer d'animation permanent au repos. Telechargements inchanges, animation non raccordee.

## Compiler et verifier

Preparation initiale des outils locaux : `setup.ps1` (w64devkit et SDK WebView2). Puis :

```powershell
python build-lottie.py
.\build.ps1 -OutputDirectory build\lottie-polish
python test-lottie.py
```

ThorVG et ses outils de build sont telecharges depuis leurs sources officielles, versions et SHA-256 figes. Aucun installateur systeme, changement du PATH global ou politique Windows. Si la politique locale interdit les scripts PowerShell, le build isole fourni est deja pret ; les scripts Python de tests invoquent directement le compilateur local.

Le moteur configure charge les animations locales autonomes du projet (shapes/paths) ; expressions et ressources externes image/font ne sont pas actives. Les deux assets sont certifies ici, sans promesse de compatibilite avec tous les effets Lottie.

## Validation et limites

Compilation isolee et cinq fixtures natives reussies : rendu de vrais frames JSON, alpha, DPI, cache borne, lifecycle, arret au repos, declenchement de copie apres succes, geometrie et regressions UI/favicon. Aucun lancement du navigateur ou chargement WebView2 durant ces verifications. Voir [rapport natif](design/lottie-integration-validation.md) et [contrat des assets](design/lottie-spec.md).

Le diagnostic precedent `Smoke download dialog did not open : 0x80004005` reste en suspens. `test.ps1` est le smoke moteur suspendu ; son assertion reste inchangee. Le rendu en fenetre reelle, le pacing du timer et la navigation complete restent a verifier.

## Paper

[Design system](https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-2-0) : synchronisation partielle. Les tokens et la marque ont ete mis a jour avant que le quota MCP hebdomadaire bloque les composants et timelines restants. Voir [reprise precise](design/paper-sync-pending.md). Les anciens canvas utilisateur sont conserves.
