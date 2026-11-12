# Éléments manquants — lot B (sandbox, recherche, erreurs, toasts, info-bulles, plein écran)

Source : Paper « Browser Design » (`01M6Q7YT14ZKMGET54ASPNGQEC`), page `p-D-0`. Lu le 2026-11-09, lecture seule
(`get_basic_info`, `get_tree_summary`, `get_jsx`, `get_computed_styles` ; captures Paper pour vérifier uniquement).
Tokens confirmés (`get_basic_info`, hash `b5366ac2`) : canvas `#171717`, raised `#292929`, hover `#333333`,
pressed `#3E3E3E`, accent `#3E67FF`, danger `#FF7B6E`, Inter Display, contrôles 32, rayons 3 / 6.

## Fichiers (propriété B)

| Fichier | Contenu |
| --- | --- |
| `src/tooltip.h` | `NativeChrome::Tooltip::Tip` (C5) **et** `NativeChrome::PopupKit`, boîte à outils commune aux surfaces B : création de popup sans ombre, horloge hôte / timer privé, glyphes Phosphor exacts de Paper (lecteur de chemin SVG → `ID2D1PathGeometry`), texte Inter avec boîtes de ligne CSS, `PlaceBelow`, rendu hors écran pour les tests |
| `src/find-bar.h` | `NativeChrome::FindBar::Bar` (C1/C2) |
| `src/context-toast.h` | `NativeChrome::ContextToast::Toast` + préréglages (A7, C8, C9) |
| `src/fullscreen-hint.h` | `NativeChrome::FullscreenHint::Hint` (C6/C7) + constantes `ExitButton` (bouton « Quitter le plein écran » du chrome F11) |
| `src/shortcuts-panel.h` | `NativeChrome::ShortcutsPanel::Panel` (C5, panneau Ctrl+/ — ajouté à la demande du lead) |
| `src/error-pages.h` | `NativeChrome::ErrorPages` (C3/C4) + `FlatJson` (JSON plat restreint) + `WebText` (échappement HTML/JS) |
| `src/debug-sandbox.h` | `NativeChrome::DebugSandbox` (page, catalogue, allowlist, pont) |
| `tests/error_pages_test.cpp`, `tests/debug_sandbox_test.cpp`, `test-sandbox.ps1` | tests |

## Conventions appliquées

- **Zéro ombre** : chaque popup passe par `PopupKit::CreatePopup` → `NativeUI::ConfigureShadowlessPopup(h, rayonPaper)`
  (région arrondie au rayon Paper exact, `DWMWCP_DONOTROUND`, pas de `CS_DROPSHADOW`). Aucun `RoundNatively`. Les surfaces
  effacent au canvas `#171717` puis peignent leur remplissage arrondi antialiasé et leur filet 1 px. Aucune ombre peinte ;
  dans le HTML, `box-shadow` n'existe que sous forme `inset` (anneau de focus Paper `#FFFFFF80`), vérifié par les tests.
- **Horloge** : `NextWake(now)` / `Frame(now)` + `wake` de l'hôte (contrat). Sans `wake`, un timer privé appelle `Frame`.
  Délais longs = échéance (`Motion::Deadline`), pas de tic 60 Hz (testé pour l'info-bulle).
- **Retour au pointer-down** (`WM_LBUTTONDOWN` arme hover/press, action au relâché sur la même cible), interactions 120–150 ms
  (`Motion::State`), entrées discrètes interruptibles, sorties en fondu ; `SetReducedMotion(true)` ⇒ fondus seuls.
- **DPI** : toutes les cotes en DIP, `GetDpiForWindow(owner)` ; `WM_DPICHANGED` repositionne.
- **UIA** : `Accessibility::Bridge` (groupe nommé, boutons, bascules `toggle`, `Notify` pour les annonces « 2 sur 7 », toasts).
- Callbacks `done` / `close` exactement une fois ; `Hide()` appelé par main ne notifie jamais.

## Mapping design → code

### C0 · Sommaire et handoff (`FWR-0`)
Nœuds lus : `FY8-0` (légende), `G2N-0` (famille C), capture. Badges repris dans la sandbox :
« À remplacer » → `rgb(62 103 255 / 16%)` + `#A9BBFF` (classe `.b-int`, réutilisée pour « Intégré ») ; « Maison » `#FFFFFF14` +
`#FFFFFFE6` (`.b-demo`) ; « Natif conservé » contour pointillé `#FFFFFF4D` + `#FFFFFFB3` (`.b-nat`) ; 13/16 medium, h 20, padding 6,
rayon 3 — `src/debug-sandbox.h` (CSS de `Html`). Liste « Natif conservé » (`G7H-0`) → `NativeWindows()`.

### C1 · Contexte recherche (`DF0-0`) et C2 · états (`DR6-0`)
Nœuds : `DNM-0` (barre in situ), `DQF-0`/`DQG-0`/`DQH-0` (info-bulle « Occurrence suivante · Entrée »), `DUL-0` (vide),
`DW5-0` (résultat, « Mot entier » actif, survol Suivant), `DX1-0` (aucun résultat), `E11-0` (focus clavier + info-bulle
« Respecter la casse · Alt+C »), notes `E4D-0`/`E69-0`.

| Valeur Paper | Code |
| --- | --- |
| barre 420×40, `top 8 / right 8` du contenu, raised `#292929` (in situ), bord 1 `#FFFFFF0F`, rayon 6, padding 4, gap 4 | `FindBar::Layout::width/height/inset/radius/surface/border` (`find-bar.h:34-40`) ; `Rest()` |
| champ flex 237×32, rayon 3, `#FFFFFF0A` ; aucun résultat `#FF7B6E14` ; padding 8, gap 8 | `fieldLeft..fieldBottom`, `fieldRadius`, `field`, `danger` ; `static_assert` 237+64+101 |
| loupe 16 @ 50 % | `Glyphs::search`, `Paint()` |
| texte 14/18 `#FFFFFFE6`, caret 1×16 `#3E67FFE6` | EDIT natif (`WM_CTLCOLOREDIT`, police GDI Inter Display 14), `NativeUI::AccentCaret` (accent .9) |
| placeholder « Rechercher dans la page » `#FFFFFF66` | dessiné après `WM_PAINT` de l'EDIT quand il est vide |
| compteur 14/18 tabulaire `#FFFFFF99`, « n sur m », « 1 sur 999+ », danger « Aucun résultat » | `CounterText()` ; `counter` |
| « Aa » 14 medium −0.02em `#FFFFFF80` (focus `E6`, actif blanc) ; « ab » 13/12 medium + crochet 16×4 (2 px, rayon 1), 50 % inactif ; actif plaque `#3E3E3E` | `Paint()` options |
| séparateur 1×16 `#FFFFFF1F` + marge 4 ; précédent/suivant/fermer 32×32 rayon 3, glyphe 16 @ 70 %, survol `#333333`, désactivé 32 % | `separatorX`, `prevLeft..closeLeft`, `glyph`, `disabled`, `Glyphs::caretUp/caretDown/close` |
| focus clavier : anneau inset 1 px `#FFFFFF80` | `PopupKit::InsetRing` |
| info-bulle sous le bouton (C1 : 6 sous la barre, 16 du bord droit) | `TipFor()` + `Tooltip::Tip` borné à la page −16 (`tooltipMargin`) |
| clavier : Entrée / Maj+Entrée, Alt+C, Alt+W, Échap, Ctrl+F refocalise ; `role=search`, compteur annoncé | `Key()`, F3/Maj+F3 en plus ; `Nodes()`, `uia_.Notify` |

États contrat : `Idle` (vide, flèches inertes), `Searching` (saisie en cours : compteur vide, flèches inertes), `Found`,
`NoMatch`, `Unavailable` (« Indisponible », non dessiné dans Paper : même gabarit que le compteur).

### C3 · Contexte hors ligne (`E72-0`) et C4 · variantes et reprise (`ECL-0`)
Nœuds : `EAY-0`/`EAZ-0` (bloc C3), `ECY-0` (DNS), `EGJ-0` (refus + détails), `EGW-0` (délai + compte à rebours), `EH9-0`
(reprise), notes `EOM-0`. Mécanisme repris de l'interstitiel HTTPS de `main.cpp` (`NavigateToString`, jeton GUID par page).

| Valeur Paper | Code (`error-pages.h`) |
| --- | --- |
| bloc 420 centré, padding-inline 16 ; titre 20/28 600 `#FFFFFFE6` équilibré ; texte 14/20 `#FFFFFF99`, hôte medium `E6`, marges 8 / 24 | CSS `main`, `h1`, `p`, `p b` |
| boutons h 32, rayon 8, padding 14, gap 8, 14 medium ; principal `#FFFFFFE6` / texte `#171717` ; secondaire `#FFFFFF14`, survol `#333333` | `.primary`, `.secondary` |
| détails : `#292929`, rayon 6, padding 8/12, gap 4 ; code 14 medium `#FFFFFFB3`, ligne 14 `#FFFFFF80` | `.details`, `Extras::code/detail`, bouton disclosure `aria-expanded` |
| statut hors ligne : point 6 rayon 3 `#FFFFFF66` + « En attente du réseau… » `#FFFFFF80` ; rétabli : point accent + « Connexion rétablie » `#FFFFFFB3` | `.status`, `.status.back`, `Extras::restored` |
| « Rechargement… » principal à 60 % | `retrying` → `aria-disabled` + `opacity:.6` |
| « Nouvel essai dans 8 s » tabulaire `#FFFFFF80`, piste 120×2 `#FFFFFF14` / remplissage `#FFFFFF80` ; 8, 16, 32 s puis arrêt | `.retry`, `AutoRetryDelay()`, action `autoRetry` |
| DNS : « Rechercher « hôte » » + Réessayer, jamais d'essai auto ; refus : détails, jamais d'essai auto | `CopyFor()` |
| hors ligne : écoute `online` puis rechargement unique | script : `addEventListener("online", …retry)` (une fois, `busy`) |
| focus initial sur l'action principale, F5 / Ctrl+R = Réessayer | `autofocus`, écouteur `keydown` |

Variantes sans maquette (copie rédigée dans le même gabarit) : `Reset` (« La connexion a été interrompue »), `Unreachable`
(« Le site est injoignable »), `Generic` (« Cette page ne s'est pas chargée »). `Classify(status, online)` : hors ligne
prioritaire ; `HOST_NAME_NOT_RESOLVED`→DNS, `CANNOT_CONNECT`→refus, `TIMEOUT`→délai, `CONNECTION_RESET|ABORTED`→réinitialisée,
`SERVER_UNREACHABLE`→injoignable, `DISCONNECTED`→hors ligne, reste→générique. `Handles()` exclut annulation, certificats
(logique HTTPS existante) et demandes d'authentification.

### C5 · Info-bulles (`EP1-0`)
Nœuds : `ERT-0`/`EST-0` (contexte Précédent + détail), `ETR-0` (variantes), notes `EW8-0..EWA-0`, et `DQF-0`/`E11-0`.

| Valeur Paper | Code (`tooltip.h`, `Tooltip::Layout`) |
| --- | --- |
| `#3E3E3E`, bord 1 `#FFFFFF0F`, rayon 6 | `surface`, `border`, `radius` (région) |
| une ligne h 28, padding 8 (4 à droite avec touche), gap 8, libellé 14/20 `#FFFFFFE6` | `height`, `padX`, `padKey`, `gap`, `label` |
| touche h 20, rayon 3, `#FFFFFF14`, padding 6, 13/20 medium `#FFFFFFB3`, juste après le libellé | `keyHeight`, `keyPad`, `keySize`, `keyPlate`, `keyText` |
| multi-ligne : padding 6/8, gap 4 (en-tête + détail) ou 2 (onglet tronqué), max 320, détail `#FFFFFF80` | `padY`, `headerGap`, `titleGap`, `maxWidth`, `detail` |
| 600 ms de survol immobile, puis instantané 300 ms entre voisins ; fondu 120 ms sans déplacement | `delayMs`, `neighbourMs`, `fadeMs` ; `Hover()` |
| 6 px sous la cible, aligné à gauche, bascule au-dessus / s'aligne au bord | `offset`, `PopupKit::PlaceBelow` (testé) |
| focus clavier : immédiat ; Échap masque sans déplacer le focus | `Show()` ; l'hôte appelle `Hide()` |

Fenêtre `WS_EX_NOACTIVATE | WS_EX_TRANSPARENT`, `WM_NCHITTEST → HTTRANSPARENT`, `MA_NOACTIVATE` : ne prend jamais le focus ni
les clics (testé).

### C5 · Raccourcis clavier Ctrl+/ (`EWC-0`)
| Valeur Paper | Code (`shortcuts-panel.h`, `ShortcutsPanel::Layout`) |
| --- | --- |
| 560 de large, `#292929`, bord `#FFFFFF0F`, rayon 6 | `width`, `radius` |
| en-tête padding 12 / 16 / 12, titre 16/20 medium `E6`, fermer 24 rayon 3 glyphe 14 `#FFFFFFB3` | `header`, `close`, `closeGlyph` |
| filtre : champ h 32 `#FFFFFF0A`, bord `#FFFFFF1A`, rayon 6, padding 8, loupe 14 `B3`, placeholder `#FFFFFF66` | `Field()`, EDIT natif, filtre insensible casse/accents (`FindNLSStringEx`) |
| section : haut 8, latéral 12 ; titre 14/20 `#FFFFFF80` (padding 8/8/4) ; ligne h 32, padding 8, rayon 6, survol `#333333` | `sectionTop`, `heading`, `row`, `rowRadius` |
| touches gap 4, h 24, min 24, padding 6, rayon 3, `#FFFFFF14`, 13/16 medium `#FFFFFFCC` | `keyGap`, `keyHeight`, `keyMin`, `keyPad` |

Contenu : seuls les raccourcis réellement gérés par `Key()` de `main.cpp` (lu le 2026-11-09). Écart assumé : les lignes Paper
« Recharger sans le cache (Ctrl+Maj+R) », « Copier l'adresse de la page (Ctrl+Maj+C) » et « Enregistrer la page (Ctrl+S) »
ne sont pas listées tant que le navigateur ne les gère pas (afficher un raccourci inexistant serait faux).

### C6 · Contexte plein écran vidéo (`F40-0`) et C7 · entrée, sortie, F11 (`F9K-0`)
Nœuds : `F7E-0` (pastille), `FBJ-0`/`FBV-0`/`FC8-0`/`FCL-0` (séquence + clavier verrouillé), `FI8-0` (bouton F11), notes `FIV-0`.

| Valeur Paper | Code (`fullscreen-hint.h`) |
| --- | --- |
| centrée, 24 du haut, h 40, `#292929EB`, bord `#FFFFFF14`, rayon 6, padding 16/8, gap 8 | `Layout::top/height/radius/padLeft/padRight/gap/border` |
| texte 14/20 `#FFFFFFB3`, origine medium `E6` ; séparateur 1×16 `#FFFFFF1F` ; touche h 24 rayon 3 `#FFFFFF1F` padding 8, 13/16 medium blanc ; « pour quitter » + 8 | `CopyFor()`, `PopupKit::DrawRich`, `keyHeight/keyPad/keyRadius/trailing` |
| clavier verrouillé : « Maintenez [Échap] », remplissage accent sur 1,5 s | `keyboardLocked`, `SetHoldProgress()`, `holdMs` |
| entrée 8 px + fondu 160 ms, visible 3 s, sortie fondu 200 ms, revient si la souris touche le haut | `slide/enterMs/visibleMs/exitMs`, `Reveal()` |
| bouton « Quitter le plein écran » F11 : h 32 `#333333` rayon 6, padding 8/4, gap 8, glyphe 16 @ 80 %, touche « F11 » h 24 `#FFFFFF14` padding 6 ; révélé après 300 ms, replié 600 ms | `FullscreenHint::ExitButton` (constantes pour le painter du lead), `Glyphs::cornersIn` |

### A7 · Toast limite de 128 onglets (`F80-0`)
Nœuds : `FD6-0` (contexte), `FGX/FH9/FHL/FHX-0` (normal, survol, focus, ouverture partielle), note `FIS-0`.

| Valeur Paper | Code (`context-toast.h`) |
| --- | --- |
| 269 de large, `#171717`, bord `#FFFFFF0D`, rayon 10, padding 16, gap 16, ancré 14 bas-gauche | `cardWidth`, `radius`, `cardPad`, `cardGap`, `inset`, `Kind::Card` |
| en-tête : info 16 @ 50 %, gap 6, titre 16/20 medium −0.01em `E6` | `Glyphs::info`, `headerGap`, `Title()` |
| texte 16/20 +0.01em `#FFFFFFB3`, 3 lignes | `PopupKit::DrawText(…, wrap)` |
| bouton pleine largeur h 32 rayon 6 `#292929`, 14/18 medium −0.01em `#FFFFFFCC` ; survol `#333333` texte blanc ; focus inset `#FFFFFF80` | `cardButton`, `buttonRadius` |
| 6 s, pause survol/focus, Échap ferme, un seul toast, annonce polie, entrée +12 px amortie | `TabLimitSpec()`, `PartialRestoreSpec()`, `cardRise`, `cardEnterMs` |

Taille vérifiée par test : 269×178 (= Paper).

### C8 · Toasts d'état (`FKX-0`) et C9 · « Page enregistrée » (`G98-0`)
Nœuds : `FLE-0` (réussite `FLK`, échec `FLY`, en cours `FMD`, bref `FMS`, focus `FN7`), notes `FVV-0`, `GCG-0`/`GCH-0` (ancrage).

| Valeur Paper | Code (`context-toast.h`) |
| --- | --- |
| 360, `#292929`, bord `#FFFFFF0D`, rayon 10, padding 8 / 12 / 8, gap 10, ancré 14 bas-gauche (gap 8 entre toasts) | `statusWidth`, `padY/padLeft/padRight`, `gap`, `Kind::Status` |
| glyphes 16 : réussite `#7D98FF`, échec danger, en cours anneau 1,5 `#FFFFFF26` + arc blanc, lien `#FFFFFFB3` | `Icon`, `success`, `DrawIcon()` |
| titre 14/20 medium `E6`, détail 14/20 `#FFFFFF80` une ligne (ellipse) | `Paint()` |
| action h 32 padding 10 rayon 6 `#FFFFFF14` 14 medium `E6` ; fermer 24 rayon 3 glyphe 14 `#FFFFFF80`, survol `#333333` ; focus inset | `action`, `actionPad`, `close`, `closeRadius`, `closeGlyph` |
| bref : largeur au contenu, h 40, padding 12 / 16 | `Kind::Brief`, `briefRight` (119×40 « Lien copié » = Paper) |
| durées : bref 3 s, réussite 6 s, échec / en cours sans délai ; entrée 8 px + 160 ms | `BriefSpec`, `PageSavedSpec`, `SaveFailedSpec`, `SavingSpec`, `statusRise`, `statusEnterMs` |
| F6 atteint les toasts, Échap ferme celui focalisé, `role=status` / `alert` | `Focus()`, `WM_KEYDOWN`, `uia_.Notify(…, ToastStatus/ToastAlert)` |

Notifications web et sélecteur « Enregistrer sous » : natifs conservés (aucune UI).

## Écarts assumés

1. **Fond de la barre de recherche** : C1 (in situ) `#292929`, C2 (planche) `#171717` : le contexte fait foi → raised.
2. **« Aa » et placeholder** en Inter Display (Paper : `system-ui`) : une seule famille dans le chrome ; même taille et graisse.
3. **Pastille plein écran opaque** `#292929` au lieu de `#292929EB` : une fenêtre layered s'estompe en bloc ; l'alpha par pixel
   (UpdateLayeredWindow) a été écarté pour tous les popups par le lot A. La note Paper dit d'ailleurs « fond opaque ».
4. **Variante F11 de la pastille** (« Plein écran | [F11] pour quitter ») : Paper ne dessine pour F11 que le chrome révélé ; la
   pastille reprend le gabarit C6/C7 et l'annonce « Plein écran, … ».
5. **Titre des pages d'erreur** en Inter Display Medium (aucun fichier SemiBold livré) : `@font-face` medium déclaré pour 500–600,
   `font-synthesis:none` (pas de faux gras).
6. **Pas de debounce UI** dans la recherche (consigne) ; la note Paper « débounce 80 ms » relève du moteur (main peut coalescer).
7. **Rayons des popups** : rayon Paper exact via région 1 bit de A (6 ou 10) ; coin en escalier atténué en effaçant au canvas.
8. **Variantes d'erreur sans maquette** (réinitialisée, injoignable, générique) : copie rédigée dans le gabarit C4.
9. **Survol du bouton d'action C8** : non dessiné dans Paper ; dérivé (+6 % de blanc, pressé +12 %).

## Pont de la sandbox

- Page : `DebugSandbox::Html(token, fontRegular, fontMedium)` (UTF-8) servie par main à `https://browser-debug.invalid/sandbox`.
- Message : chaîne `JSON.stringify({token, command, variant?})` ; `ParseCommand` : ≤ 512 caractères, objet plat de chaînes
  uniquement, clés `token`/`command`/`variant` seulement (aucune autre, pas de doublon), jeton comparé sans sortie anticipée,
  commande **et** variante dans l'allowlist. La forme JSON d'une chaîne (`get_WebMessageAsJson`) est aussi acceptée.
- Journal : `ResultScript(line)` → `window.__sandboxLog("…","natif")` ; échappement JS (`\\ \" \'`, contrôles, U+2028/2029,
  `< > &` en `\uXXXX`), ligne bornée à 2000 caractères ; aller-retour testé sur des chaînes hostiles.
- Au chargement, la page envoie `sandbox.ping` : main répond `ResultScript("Pont actif")`.

### Commandes (`DebugSandbox::Commands()`)

| Commande | Variantes | Effet attendu côté main |
| --- | --- | --- |
| `modal.generic` | default, danger, disabled, error, busy | A1, vraie modale, contenu démo |
| `modal.alert` | default, long | A2, origine démo |
| `modal.confirm` | default | A2 |
| `modal.prompt` | default, error | A3 (error = validation en échec) |
| `modal.beforeUnload` | leave, reload | A4 |
| `modal.clearData` | default, busy | A5 en mode Démonstration (rien n'est effacé) |
| `modal.closeWindow` | default | A6 (rien n'est fermé) |
| `modal.openManyTabs` | default | A6 |
| `modal.deleteBookmarks` | one, many | A6 (rien n'est supprimé) |
| `modal.quit` | default | A6 (rien n'est fermé) |
| `toast.tabLimit` | blocked, partial | A7 (`TabLimitSpec` / `PartialRestoreSpec`) |
| `modal.about` | upToDate, checking, available, error | A8 |
| `modal.private` | current, concept | A9 |
| `prompt.permission` | camera, microphone, cameraMicrophone, geolocation, notifications, clipboard, blocked | B01/B02, origine démo |
| `prompt.passwordFill` | one, two | B03, comptes fictifs |
| `prompt.passwordSave` | save, update | B04 |
| `password.manager` | list, detail, delete | B05, données en mémoire |
| `prompt.basicAuth` | default, error | B06 |
| `prompt.externalApp` | default | B06 |
| `prompt.dangerousDownload` | default | B07 |
| `prompt.clientCertificate`, `prompt.screenShare`, `prompt.keepFile` | default | B08 (concepts) |
| `find.open` | default, seed | C1/C2 (seed = « oxyde ») |
| `error.page` | nameNotResolved, refused, timedOut, reset, unreachable, generic | C4, nouvel onglet `*.invalid` + `ErrorPages::Html` |
| `error.offline` | offline, retrying, restored | C3 |
| `tooltip.show` | label, shortcut, detail, tab, disabled | C5, vraie info-bulle |
| `shortcuts.open` | default | C5, panneau Ctrl+/ |
| `fullscreen.hint` | page, browser, locked | C6/C7, pastille seule |
| `toast.status` | success, failure, progress, brief | C8 |
| `toast.pageSaved` | default | C9 |
| `sandbox.ping` | (aucune) | réponse « Pont actif » |

### Statut de chaque entrée de la sandbox (`DebugSandbox::Catalogue()`, corrigé par le lead le 2026-11-09)

| Entrée | Statut | Chemins réels dans la page | Variantes démo marquées |
| --- | --- | --- | --- |
| C0 | Sommaire | — | — |
| A1 | Démonstration | — | toutes |
| A2 | Intégré | `window.alert`, `window.confirm` | — |
| A3 | Intégré | `window.prompt` | — |
| A4 | Intégré | interrupteur `beforeunload` + recharger / quitter | — |
| A5 | Intégré (+ commande en mode Démonstration) | — | — |
| A6 | Intégré + Démonstration | — | deleteBookmarks, quit |
| A7 | Intégré + Démonstration | — | partial |
| A8 | Intégré + Démonstration | — | checking, available, error |
| A9 | Intégré + Démonstration | — | concept |
| B01 | Intégré | `getUserMedia` caméra + micro | — |
| B02 | Intégré | caméra, micro, géolocalisation, notifications, presse-papiers | — |
| B03, B04, B05 | Démonstration | — (comptes fictifs `camille.demo@exemple.test`) | — |
| B06 | Intégré | lien `mailto:` fictif | — |
| B07, B08 | Démonstration | — | — |
| B09 | Note | — | — |
| C1, C2 | Intégré | bloc « Texte pour la recherche » (répétitions, accents, casse) | — |
| C3, C4 | Intégré | lien `https://hote-inexistant.invalid/` | — |
| C5 | Démonstration + Intégré | — | panneau Ctrl+/ marqué intégré |
| C6, C7 | Intégré | `requestFullscreen` sur un canvas animé, `navigator.keyboard.lock(["Escape"])` | — |
| C8, C9 | Démonstration | — | — |
| Natif conservé | Natif conservé | sélecteur de fichiers, impression, Windows Hello, centre de notifications, partage Windows | — |

Accès documenté dans la page : `browser://debug`, Ctrl+Maj+F12, `--debug-sandbox`.

## Tests

`powershell -NoProfile -ExecutionPolicy Bypass -File test-sandbox.ps1` (dossier `build/sandbox`) :
1. chaque en-tête B compile seul, puis tous ensemble avec `chrome.h`, `-Wall -Wextra`, **zéro warning** (échec sinon) ;
2. `tests/error_pages_test.cpp` — 62 vérifications : `Classify`, `Handles`, `HostOf`, copie de chaque variante, compte à rebours
   8/16/32 puis arrêt, échappement d'URL / d'hôte / de jeton hostiles, CSP, aucune ombre décorative, pont (jeton faux/absent/vide,
   action inconnue, casse, clé en trop, doublon, valeur imbriquée, JSON tronqué, déchets, forme legacy, longueur) ;
3. `tests/debug_sandbox_test.cpp` — 301 vérifications : allowlist (unicité, chaque déclencheur autorisé, pas de commande
   générique), `ParseCommand` (jeton faux/absent/vide/préfixe, commande/variante hors liste, variante manquante ou en trop, clé en
   trop, tableau, JSON tronqué, guillemets simples, `\u0000`, message trop long), aller-retour `ResultScript`, page (28 artboards,
   statuts, accès, chemins web réels, contenu de recherche, jeton unique, jeton hostile, aucune ombre) ; harnais natif sur une
   fenêtre propriétaire hors écran : glyphes Paper, barre (ancrage, 420×40, requêtes, Entrée/F3/Alt+C/Alt+W, états, Échap → close
   une fois, `Hide` muet), toasts (269×178, 360×58, 40, ancrage 14, callback une fois sur action / remplacement / délai / Échap /
   `Dispose`, échec persistant, mise à jour), info-bulle (délai 600 ms par échéance, voisins instantanés, `PlaceBelow`, jamais
   active), pastille (copie, centrage 24, sortie après 3 s, maintien Échap), panneau Ctrl+/ (filtre casse/accents, Échap) ; tous
   `NativeUI::IsShadowless`. Rendus PNG régénérés : `build/sandbox/render_*.png` (même `Paint()`, cible WIC, 192 dpi).

Comparaison visuelle effectuée sur ces rendus (barre de recherche, toasts A7/C8, pastille verrouillée, info-bulle détaillée,
panneau) face aux captures Paper : gabarits, couleurs et glyphes conformes ; correction faite pendant la revue : la touche de
l'info-bulle détaillée suit le libellé (Paper `EST-0`).

## Non vérifié / reste à faire

- Intégration dans `main.cpp` (lead) : rien n'a été lancé dans le navigateur ; le rendu GPU réel, la DPI 125/150 % à l'écran, le
  comportement d'activation de la barre au-dessus de WebView2 et les annonces Narrateur n'ont pas été observés.
- La boucle de messages de `main.cpp` passe `WM_KEYDOWN/WM_SYSKEYDOWN` à `Key()` avant dispatch : le lead doit laisser passer
  Entrée, Échap, Tab, F3, Alt+C/W et les flèches quand la barre ou le panneau a le focus.
- Le texte de l'EDIT n'apparaît pas dans les PNG hors écran (contrôle GDI enfant) ; il est couvert par les tests de comportement.
- Le test de l'info-bulle place la fenêtre dans la zone de travail du moniteur (elle peut apparaître brièvement à l'écran).
