# Vérification native des corrections UI

Date : 6 novembre 2026. Cette étape est indépendante du diagnostic WebView2 suspendu. Aucun lancement de `browser.exe`, test WebView2, panneau de téléchargements ou changement de runtime n’a été effectué.

## Corrections

- **Texte rogné verticalement** : les anciennes boîtes de 14 DIP recevaient des polices de 14/16 DIP dont la hauteur de ligne réelle est supérieure, puis `D2D1_DRAW_TEXT_OPTIONS_CLIP` coupait l’encre. DirectWrite reçoit maintenant une ligne mesurée et le contrôle complet de 32 DIP ; le dessin est aligné à gauche, centré verticalement avec une baseline stable et protégé par les métriques de débordement. L’ellipse s’applique uniquement au dépassement horizontal réel. La ligne Inter Display 16 mesure **19,359 DIP** dans le harnais. L’EDIT natif utilise ses métriques GDI complètes et sa baseline est alignée sur DirectWrite, au pixel arrondi.
- **Fermeture d’onglet** : Phosphor X Bold à droite, slot réservé et cible de **24 × 24 DIP**, rectangle blanc original conservé exactement à gauche. Le titre ne change pas de largeur au survol. Espaces d’identifiants séparés : onglets 1000–1127, fermetures 2000–2127. Fermer un onglet inactif préserve l’identité et le focus de l’onglet actif ; clic milieu et menu utilisent la même fermeture ciblée. La sélection après fermeture est vérifiée exhaustivement pour 1 à 128 onglets.
- **Double survol** : une seule surface opaque par onglet ; le X n’ajoute pas de rectangle de fond. Les cibles d’animation regroupent onglet/X, et les valeurs sont combinées avec `max`, jamais additionnées. Les captures du harnais mesurent `#292929` sous le X de l’onglet sélectionné et `#222222` pour le survol inactif, sans seconde composition.
- **Bip Entrée** : `TranslateMessage` produit un `WM_CHAR` même quand le `WM_KEYDOWN` est consommé par la validation. Un état de touche absorbe seulement les caractères Entrée/Échap/Tab correspondant à une action traitée ; les autres caractères et la composition IME restent transmis. `WM_GETDLGCODE` précise les touches attendues. Le test utilise une vraie fenêtre EDIT cachée et une boucle `TranslateMessage/DispatchMessage`, sans navigation, et vérifie que son unique `WM_CHAR` Entrée est consommé avant la procédure EDIT par défaut.
- **Cadre blanc** : le code précédent laissait le calcul nonclient initial (`WM_NCCALCSIZE` avec `wParam=FALSE`) et les repeints nonclient/activation passer à Windows, bien que toute la fenêtre soit dessinée en client. Ces chemins sont maintenant tous pris en charge ; le rendu nonclient DWM est désactivé pour cette seule fenêtre. `WS_THICKFRAME`, les styles minimiser/maximiser et les hit-tests de redimensionnement restent présents ; `HTMAXBUTTON` permet aussi les comportements Snap de Windows. Le harnais vérifie les chemins et les styles, mais **l’absence de bordure blanche sur le bureau réel reste à confirmer** après autorisation de lancement.

## Icônes et animations

Toutes les icônes d’interface viennent du paquet officiel **`@phosphor-icons/core` 2.1.1**, poids **Bold**, sous MIT : `https://registry.npmjs.org/@phosphor-icons/core/-/core-2.1.1.tgz`. SHA-256 de l’archive : `313332BE6190B724DA24107ADDD781799B48BF76B13963F24501112FFE1BAADD`. Les SVG originaux et la licence sont dans `assets/icons/phosphor-bold`. `design/render-icons.cjs` fabrique les PNG à huit résolutions avec le moteur local resvg ; aucune dépendance npm à l’exécution. L’icône d’application Paper et le nom **browser** sont conservés.

Le module `src/motion.h` du worker dédié est intégré : survol 120 ms, pression 80 ms, capture souris et annulation à la perte de capture/activation, prise en compte de l’option Windows de mouvement réduit, et coche Phosphor Bold après réussite réelle de `SetClipboardData`. Le timer 72 est distinct du timeout smoke 1, ne repeint que les 84 DIP de chrome et s’arrête au repos ; les deadlines de maintien ne tournent pas en boucle à 60 Hz. Le modèle d’animation de téléchargement reste **dormant**, sans connexion aux API WebView2 en attente, ni fausse progression ou indication de réussite.

## Preuves

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\test-ui.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -OutputDirectory build\ui-fixes
```

Résultats : **712 059 assertions UI natives réussies**, principalement les cas exhaustifs de sélection après fermeture, et **54 tests déterministes d’animation réussis** ; sorties 0 et compilation avec avertissements traités comme erreurs pour les harnais. Build complet du navigateur réussi, sans exécution. Aucun chargement de `WebView2Loader.dll` dans le harnais.

Les DPI testés sont 96, 120, 144, 168, 192, 240, 288 et 384 (100–400 %). Vérifications : encre de `G`, `Google`, descendantes et accents, ellipse horizontale, alignement de baseline de l’EDIT, zones de fermeture à trois largeurs et sélection débordante, couleurs de survol statiques/animées, chemins nonclient et séquence Entrée. Les tests n’injectent pas d’URL dans le navigateur et ne mesurent pas le son du système.

Captures exportées par le **même renderer natif** sur une surface WIC hors écran : `build/native-qa/hover-tab-96.png`, `hover-close-96.png`, versions jusqu’à 384 DPI et `copy-check-96.png`. Revue visuelle effectuée à 96 et 192 DPI et sur la coche de copie. Ce sont des rendus natifs de contrôle, **pas des captures d’une application relancée**. Les clics réels, le redimensionnement/Snap sur le bureau et le résultat complet dans WebView2 restent non certifiés.

Le problème antérieur reste inchangé : `WEBVIEW2 ERROR Smoke download dialog did not open : 0x80004005`. Le test smoke complet conserve cette assertion ; il n’a pas été affaibli ni réexécuté. Aucun contournement ou diagnostic de ce problème n’a été tenté.
