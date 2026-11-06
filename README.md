# browser — navigateur Windows natif

**Candidat suivant, non déployé** : les deux causes de couleur de survol ont été corrigées et le favicon réel par onglet a été intégré via l’API officielle. Build séparé : `build/hover-favicon-candidate/browser.exe`. Tests ciblés : `test-hover-favicon.ps1`. L’instance existante et son exécutable `build/v2` restent inchangés ; ce candidat n’a pas été lancé. Voir [les causes, mesures et limites précises](design/hover-favicon-findings.md). La demande de vrai favicon remplace le placeholder blanc décrit ci-dessous pour la version précédente. Le diagnostic de téléchargements reste en attente.

Interface C++17/Win32 : Direct2D/DirectWrite pour le chrome, WebView2 pour les sites. Le nom **browser** et son icône personnalisée Paper sont conservés.

## État actuel

Les corrections UI et animations sont compilées dans `build/ui-fixes/browser.exe`, puis copiées vers `build/v2/browser.exe` avec leurs ressources. **Le navigateur n’a pas été relancé pendant cette étape.** Le raccourci existant garde sa cible `build/v2/browser.exe`, sans réinstallation ; `build/v2/profile` est conservé.

Le diagnostic WebView2 antérieur reste en attente : `WEBVIEW2 ERROR Smoke download dialog did not open : 0x80004005`. Ne pas réexécuter le smoke complet ni tenter de correction ou relance dépendante sans autorisation explicite. La validation de cette étape est exclusivement native, hors moteur WebView2.

## Interface et corrections

- Fond vide `#171717`, chrome de 84 DIP, contrôles natifs dessinés d’après Paper V2 puis corrigés selon les retours utilisateur.
- Polices locales Inter Display et Gelasio, texte complet verticalement et aligné à gauche ; ellipse uniquement pour les vrais titres trop longs. L’EDIT natif conserve une ligne complète et une baseline assortie.
- Rectangle blanc de favicon conservé à gauche ; X Phosphor Bold à droite au survol, emplacement réservé et cible de 24 DIP. Fermer un onglet inactif préserve l’onglet actif.
- Survol sans double fond ; caractère Entrée traité sans passer à la procédure EDIT par défaut ; gestion du cadre Windows nonclient corrigée en conservant les styles de redimensionnement/Snap.
- Adresse/recherche, précédent/suivant, actualiser, copier le lien, liste et fermeture d’onglets. `Ctrl+L/T/W`, `Ctrl+Tab` / `Ctrl+Maj+Tab`, `Ctrl+R` / `F5`, `Alt+←/→`. Maximum 128 onglets ; liste par le logo et molette sur la barre.
- Toutes les icônes d’interface sont **Phosphor Icons Bold 2.1.1**, SVG originaux et licence MIT dans `assets/icons/phosphor-bold`. L’icône d’application reste personnalisée.
- Animations natives de survol/pression et coche après copie réussie, mouvement réduit respecté et timers arrêtés au repos. Les animations de téléchargement restent dormantes, sans progrès fictif.
- La navigation privée est explicitement indisponible ; le profil normal conserve cookies/cache. Le panneau de téléchargements garde son code existant, non validé à cause du diagnostic en attente.

## Compiler et vérifier sans lancer WebView2

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\setup.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -OutputDirectory build\ui-fixes
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\test-ui.ps1
```

Dépendances de build locales : w64devkit 2.11.0 / GCC 16.2.0 et SDK Microsoft.Web.WebView2 1.0.4314.35 depuis leurs sources officielles. Aucun installateur système ni changement du PATH. `Bypass` vaut uniquement pour le processus PowerShell lancé. Les fontes et images sont fournies dans le projet. Régénération des icônes : `npm install --prefix tools/render @resvg/resvg-js@2.6.2 --no-audit --no-fund`, puis `node design/render-icons.cjs` ; npm n’est pas requis à l’exécution.

Résultats du 6 novembre 2026 : **712 059 assertions UI natives** (principalement les cas exhaustifs de sélection après fermeture) et **54 tests déterministes d’animation**, huit DPI de 100 à 400 %, tous réussis ; build complet du navigateur réussi. Les harnais créent des surfaces Direct2D/WIC et des contrôles Windows cachés, sans charger WebView2. Rendus inspectés : `build/native-qa/hover-close-96.png`, `hover-close-192.png`, `copy-check-96.png`. Les clics réels, le son, le cadre sur le bureau et l’application complète relancée ne sont pas certifiés.

Voir [le rapport détaillé](design/ui-fixes-validation.md) et [le contrat du module d’animation](design/motion-integration.md). **`test.ps1` est le smoke WebView2 suspendu ; il ne fait pas partie de cette validation native et son assertion n’a pas été affaiblie.**

## Fichiers et données

Conserver `WebView2Loader.dll`, `assets` et les licences à côté de `browser.exe`. Le runtime WebView2 doit déjà être présent. Les données normales sont dans `profile` à côté de l’exécutable, sans import du profil Edge. La préparation et le build n’exécutent pas le navigateur. `tools`, `deps`, `build` et profils sont exclus de Git.

Pas de mots de passe, extensions, synchronisation, gestion avancée des favoris ou navigation privée dans ce prototype. Les erreurs WebView2 sont journalisées et arrêtent les commandes dépendantes : transmettre le diagnostic avant tout correctif, contournement ou nouvelle tentative. Aucun navigateur par défaut, réglage système ou runtime n’a été modifié.
