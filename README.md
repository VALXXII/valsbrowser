# browser ? Windows natif

Version int?gr?e sur `main`, issue de `feat/design-system-polish`. La branche historique `master` est conserv?e.

## Version Lottie + polish

La recherche Windows **browser** utilise d?sormais **`build/v2/browser-main.exe`**, avec le profil existant `build/v2/profile`. Fermer puis rouvrir le navigateur pour charger cette version. Le paquet autonome **`build/lottie-alignment/browser.exe`** est ?galement disponible ; son profil serait distinct dans `build/lottie-alignment/profile`. Les deux dossiers contiennent la DLL officielle, les fontes, les ic?nes, les animations et les licences.

- Onglets : gap visible de 6 DIP, comme les boutons ; largeur 128, pas 134.
- Croix d'onglet : cible 24 x 24, inset egal de 4 DIP, glyphe Phosphor Bold 16.
- Focus clavier : bordure blanche de 1 DIP, opacite 10 %, selon le choix utilisateur.
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
