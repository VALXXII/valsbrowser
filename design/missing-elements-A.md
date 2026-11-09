# Éléments manquants — spécialiste A (modales, popovers d'origine, zéro ombre)

Source : Paper « Browser Design » (`01M6Q7YT14ZKMGET54ASPNGQEC`), page `p-D-0`, lue le 2026-11-09
(tokens `b5366ac2`). Valeurs exportées avec `get_jsx` / `get_node_info` / `find_nodes` ;
captures Paper uniquement pour comparer. Aucune écriture Paper.

Fichiers : `src/native-ui.h`, `src/modal.h` (nouveau), `src/origin-prompt.h` (nouveau),
`src/password-demo.h` (nouveau), helper sans ombre appliqué dans `bookmark-toast.h`,
`bookmark-popover.h`, `bookmarks-panel.h`, `context-menu.h`, `download-panel.h`,
`history-panel.h`, `settings-panel.h`, `address-popup.h`, `update-toast.cpp` ; tests
`tests/popup_shadow_test.cpp`, `tests/modal_test.cpp`, `test-popups.ps1`.

## 1. Zéro ombre portée

### Mesures (Windows 11 10.0.26200, bureau déverrouillé, début de session)

Sonde `build/popups/shadow_probe*.cpp` : popup #171717 300×200 sur un fond blanc topmost,
pixels de l'écran composé lus avec `GetPixel(GetDC(nullptr))` (ce que DWM affiche, ombre
comprise — `PrintWindow` ne la voit pas).

| Configuration de la popup | 2 px sous le bord | 6 px | 12 px | 4 px à droite | Coin |
| --- | --- | --- | --- | --- | --- |
| `WS_POPUP`, DWMWCP_DEFAULT | `#FFFFFF` | `#FFFFFF` | `#FFFFFF` | `#FFFFFF` | carré |
| DWMWCP_ROUND (ancien `RoundNatively`) | `#CBCBCB` | `#D0D0D0` | `#D8D8D8` | `#E9E9E9` | AA |
| ROUND + `DWMNCRP_DISABLED` (avant ou après affichage) | idem ombre | | | | AA |
| ROUND + `WS_EX_LAYERED` (alpha ou colour key) | idem ombre | | | | AA |
| ROUND + `WS_EX_NOREDIRECTIONBITMAP` / `DwmExtendFrameIntoClientArea(-1)` | idem ombre | | | | AA |
| ROUND + `UpdateLayeredWindow` per-pixel | idem ombre | | | | AA |
| DWMWCP_ROUNDSMALL (± NC disabled) | `#E6E6E6` | `#EAEAEA` | `#F5F5F5` | `#FBFBFB` | AA |
| `CS_DROPSHADOW` (± NC disabled) | `#D4D4D4` | `#FFFFFF` | | `#FCFCFC` | carré |
| `WS_THICKFRAME` + WM_NCCALCSIZE=0 | `#BBBBBB` | `#C0C0C0` | `#C8C8C8` | `#E6E6E6` | AA |
| **DWMWCP_DONOTROUND + région `CreateRoundRectRgn`** (± layered) | `#FFFFFF` | `#FFFFFF` | — | `#FFFFFF` | **arrondi 1 bit** |
| `UpdateLayeredWindow` per-pixel seul | `#FFFFFF` | `#FFFFFF` | `#FFFFFF` | `#FFFFFF` | AA possible |
| info-bulle comctl **v5** (celle de l'appli, sans manifeste v6) | `#FFFFFF` | `#FFFFFF` | — | `#FFFFFF` | `WS_BORDER` |

Conclusion : l'arrondi antialiasé de DWM et son ombre sont indissociables par API publique.

### Politique retenue (`src/native-ui.h:86-191`)

* `ConfigureShadowlessPopup(HWND, float radius = keepRegion)` : retire `CS_DROPSHADOW` de
  la classe (cette classe, ce processus), `WS_DLGFRAME|WS_THICKFRAME`, pose
  `DWMNCRP_DISABLED`, `DWMWCP_DONOTROUND`, bordure DWM `COLOR_NONE` ; radius > 0 ⇒ région
  arrondie au rayon Paper, recalculée à chaque `WM_WINDOWPOSCHANGED` (taille) et
  `WM_DPICHANGED` ; `keepRegion` pour les tiroirs qui gèrent déjà leur région ; 0 = carré.
  Un sous-classement comctl réaffirme les attributs sur `WM_NCACTIVATE`, `WM_SHOWWINDOW`,
  `WM_THEMECHANGED`, `WM_DWMCOMPOSITIONCHANGED`, `WM_DPICHANGED`. État en propriété de
  fenêtre (`SetPropW`) : **pas de `GetWindowSubclass`**, exporté seulement par ordinal par
  le comctl32 v5 que charge l'appli (l'importer par nom empêchait l'exe de démarrer,
  0xC0000139 — corrigé, l'exe statique se lance).
* `IsShadowless` : faux si `CS_DROPSHADOW`, cadre de dialogue/épais, rendu NC DWM actif, ou
  préférence de coin ROUND/ROUNDSMALL.
* `StripTooltipShadow` : `CS_DROPSHADOW` retiré de `tooltips_class32` pour ce processus (v6
  l'a ; v5 non) + attributs DWM.
* `RoundNatively` reste **réservé à la fenêtre principale** (cadre de fenêtre de premier
  niveau, pas une popup).
* DWM reste actif ; aucun réglage système (`SPI_SETDROPSHADOW` interdit) ; aucune ombre peinte.

**Compromis assumé :** silhouette des coins en masque 1 bit (marches d'escalier) au lieu de
l'antialiasing DWM. Atténuation : chaque surface peint son remplissage et son filet
arrondis antialiasés en Direct2D et efface en #171717, la couleur du chrome/page sombre sous
elle, donc les pixels en marche sont presque toujours canevas sur canevas. Gain : rayon Paper
exact par surface (modales 10, popovers 8, suggestions 10) au lieu du 8 DIP fixe de DWM. La
voie per-pixel alpha (`UpdateLayeredWindow`) donnerait des coins AA sans ombre mais ne
compose pas les EDIT enfants (champs natifs) et imposerait un nouveau rendu pour chaque
surface : rejetée.

### Application

| Fenêtre | Fichier | Appel |
| --- | --- | --- |
| Toast favoris | `bookmark-toast.h:127-133` | `ConfigureShadowlessPopup(hwnd_, Layout::radius)` (remplace `RoundNatively`) |
| Popover favori | `bookmark-popover.h:357` | idem (radius 8) |
| Menu contextuel (chaque niveau) | `context-menu.h:131` | idem (radius 8) |
| Popover téléchargements | `download-panel.h:665` | idem (`Panel::radius` 8) |
| Tiroirs téléchargements / historique / réglages / favoris | `download-panel.h:806`, `history-panel.h:570`, `settings-panel.h:1361`, `bookmarks-panel.h:373` | `ConfigureShadowlessPopup(hwnd_)` (région gérée par le tiroir) |
| Suggestions d'adresse | `address-popup.h:85` + filet peint `address-popup.h:179-182` | radius 10 ; le contour que DWM dessinait (`DWMWA_BORDER_COLOR`) est maintenant peint (blanc 2 %) |
| Toast de mise à jour | `update-toast.cpp:436` | radius `Paper::radius` 8 |
| Info-bulles comctl des tiroirs/suggestions | `download-panel.h:616`, `history-panel.h:569`, `settings-panel.h:1360`, `address-popup.h:87` | `StripTooltipShadow(tip_)` |
| Modale, voile, popovers d'origine, démo B05 | `modal.h` (`Dialog::Create`) | radius 10 (panneau), 0 (voile) |
| Bulle d'état, overlay split | `status-bubble.h`, `split-overlay.h` | **inchangés** : per-pixel alpha, sans arrondi DWM ni `CS_DROPSHADOW` ⇒ déjà sans ombre, vérifié par `IsShadowless` dans le test ; ne pas les modifier évite d'ajouter `-ldwmapi -lcomctl32` à `test-status-bubble.ps1` (pas à moi). |
| Info-bulle de la fenêtre principale | `main.cpp` (lead) | `NativeUI::StripTooltipShadow(tooltip)` au WM_CREATE (ajouté par le lead). |

Fenêtres système (sélecteur de fichiers, impression, Windows Hello, menus moteur WebView2) :
non concernées, rien n'est promis sur leurs ombres.

### Preuves et tests

* `tests/popup_shadow_test.cpp` (via `test-popups.ps1`) : contrôle négatif (une fenêtre
  ROUND est détectée ombrée et la capture voit l'ombre), helper (région, coin coupé,
  redimensionnement, réaffirmation après `WM_NCACTIVATE`/`WM_DPICHANGED`/affichage, rayon 0),
  `CS_DROPSHADOW`+`WS_THICKFRAME` neutralisés, popup layered, info-bulle, et **les vraies
  fenêtres** : Modal::Dialog (panneau + voile), OriginPrompt, PasswordDemo, BookmarkToast,
  BookmarkPopover, AddressUI::Suggestions, DownloadsUI::Popover/Drawer, HistoryUI::Drawer,
  SettingsUI::Drawer, BookmarksUI::Drawer, StatusBubble, SplitOverlay.
* Dernier passage (2026-11-09 07:3x) : 27 contrôles structurels **OK** ; les 8 contrôles
  pixel = **NOT VERIFIED** : la session était verrouillée pendant ce passage
  (`BitBlt` écran → `ERROR_ACCESS_DENIED`, `GetPixel` → `CLR_INVALID`, pas de fenêtre au
  premier plan). Preuve pixel disponible : la sonde ci-dessus, exécutée bureau déverrouillé
  en début de session (région + DONOTROUND = blanc pur autour). À relancer :
  `./test-popups.ps1` sur un bureau déverrouillé.
* Menu contextuel : non instancié par le test (ses fenêtres n'existent qu'à l'ouverture
  d'un menu) — appel vérifié à la lecture (`context-menu.h:131`), *Not verified* à l'exécution.

## 2. Matériau commun (A1, `DialogKit`, `src/modal.h:58-337`)

Nœuds lus : `DF3-0` (modale), `DIU-0` (boutons × états), `DMV-0` (champ × états), `DOO-0`
(case × états), `DIP-0` (note anatomie).

| Valeur Paper | Constante |
| --- | --- |
| Surface `#171717`, bordure `#FFFFFF14` 1 px, rayon 10, largeur 400 (min 320), padding 16, pile 16, actions +8 | `Paper::width/minWidth/padding/gap/radius/border/actionsTop` (`modal.h:61-62`) |
| Voile noir 40 % — zone page (dialogue de page) ou fenêtre (dialogue navigateur) | `Paper::scrim`, `Scope::Page/Window` |
| Titre Medium 16/20 −0,01em ; description 14/20 blanc 70 % ; groupés à 4 | `Type::title/body` |
| Bouton 32, padding 12, min 64, rayon 6, 14/18 Medium −0,01em, écart 8 | `Paper::button*`, `Type::button` |
| Principal `#3E67FF` / survol `#5779FF` / pressé `#3557D6` / focus anneau inset `#FFFFFFB3` | `Kit::Button` Tone::Primary |
| Secondaire `#292929` texte `#FFFFFFCC` / survol `#333` blanc / pressé `#3E3E3E` / focus anneau `#FFFFFF80` | Tone::Secondary |
| Destructif `#FF7B6E24` texte danger / survol `#FF7B6E33` `#FF9489` / pressé `#FF7B6E42` | Tone::Danger |
| Discret transparent texte `#FFFFFFB3` / survol `#333` blanc | Tone::Quiet |
| Désactivé : opacité 32 % (calque D2D) | `Paper::disabled` |
| Champ 32, padding 10, fond blanc 8 % / survol 12 % / focus anneau 1 px blanc 50 % / invalide fond danger 8 % + anneau danger ; texte 16/20 −0,01em ; erreur 14/20 danger, icône 16, écart 8 (note `E6V-0`) | `Paper::field*`, `EditField`, `Dialog::Paint` case `B::Field` |
| Case 16, rayon 3, anneau `#FFFFFF4D` (survol `#FFFFFF80`), cochée `#3E3E3E` + coche 12 | `Kit::CheckBox` |
| Glyphes Phosphor exportés (coche, alerte, chevrons, ✕, téléchargement, fleur…) | `DialogKit::Glyph` (chemins SVG Paper rendus par `SvgGeometry`, `modal.h:94-163` et `:341-357`) |
| Badge « Démonstration » : repris du chip Paper « Concept futur » `G5O-0` (teinte split 16 %, 24 px, padding 8, 14/18 Medium) — Paper n'a pas de badge « Démonstration » | `Kit::Badge` |

Comportement (notes `DIP-0`, `DVK-0`, `E6V-0`) : focus initial sur l'action sûre, Échap = bouton
annuler (ou −1), Entrée = défaut sauf destructif ou désactivé, Tab/Maj+Tab cycliques, flèches
gauche/droite entre boutons, focus restauré (`GetFocus()` mémorisé au `Show`). Retour visuel
au pointer-down (press immédiat), survol 120 ms, pressé 150 ms (`Motion::State`), entrée
150 ms opacité + 4 DIP, sortie 120 ms opacité, non bloquantes ; mouvement réduit : opacité
seule, spinner figé. UIA : groupe nommé du titre (aide = message), boutons, cases (Toggle),
sélecteur (ExpandCollapse), comptes (ListItem), EDIT nommés par leur libellé et leur erreur.

**Écarts assumés (matériau)** : SemiBold 600 (titres 20/24) rendu en Medium : seules les fontes
Regular/Medium sont embarquées (`assets/fonts`, hors de ma propriété) ; sélection du texte dans
les EDIT en couleur système (Paper `#3E67FF73`) et suivi −0,01em non applicable aux EDIT natifs ;
coins 1 bit (§1) ; barre de défilement discrète de 2 DIP (blanc 20 %) quand le message dépasse
12 lignes (absente de Paper, nécessaire pour signaler la suite).

## 3. Modales A1–A9 (`src/modal.h`)

| Artboard | Nœuds lus | Préréglage | Points Paper repris | Écarts |
| --- | --- | --- | --- | --- |
| A1 `DEU-0` | `DF1-0`, `DIU-0`, `DMV-0`, `DOO-0` | `Dialog`, `DialogKit` | cf. §2 | — |
| A2 `DQ4-0` | `DR9-0`, note `DVK-0` | `AlertSpec`, `ConfirmSpec` (`modal.h:1320-1332`) | « <site> indique » 14/20 Medium 60 % + favicon 16 ; message 16/22 texte brut, 12 lignes puis défilement ; case « Empêcher ce site d'ouvrir d'autres dialogues » à partir du 2ᵉ dialogue (`repeated`) ; OK = défaut ; 16 DIP sous le haut de la page, centré ; voile sur la page seulement | Changer d'onglet : le lead annule (`ModalTabChanged`) au lieu de garder le dialogue en attente — décision d'intégration à confirmer côté lead |
| A3 `DWW-0` | `E0I-0`, `E2Q-0`, note `E6V-0` | `PromptSpec`, `InputSpec` (`:1333-1347`) | valeur sélectionnée à l'ouverture, aucune validation pour `prompt()` ; variante navigateur validée au 1er envoi puis à chaque frappe, action désactivée, Entrée inerte | — |
| A4 `E8Q-0` | `EBW-0` | `BeforeUnloadSpec` (`:1348`) | texte du navigateur, « Rester sur la page » focus + annuler, « Quitter »/« Actualiser » destructif ; voile onglet | — |
| A5 `ECR-0` | `EHM-0`, `ET2-0`, `ES6-0`, note `EU5-0` | `ClearDataSpec` (`:1359`) | Période (5 options Paper) 184 px, 5 catégories (3 cochées), listes à filets `#FFFFFF0D`, focus Annuler, Entrée n'efface jamais ; état occupé : spinner 16 + « Effacement… » + contrôles verrouillés (`SetBusy`) ; `Snapshot()` ; `Finish()` | Erreur d'effacement : pas d'état Paper ⇒ ligne danger au-dessus des actions (`SetError`), vocabulaire des erreurs de champ ; menu Période natif via `Hooks.choose` (lead), sinon cycle |
| A6 `EWX-0` | `EY8-0`, `F0K-0`, `F32-0`, `find_nodes EX1-0`, note `F4K-0` | `CloseWindowSpec`, `OpenManyTabsSpec`, `DeleteBookmarksSpec` (`:1371-1409`) | « Ne plus demander » ; téléchargement en cours ⇒ ligne fichier + action destructive, sans case ; limite 128 « Ouvrir 40 des 61 favoris ? » | `DeleteBookmarksSpec` : pas d'artboard Paper (demandé par la tâche), vocabulaire A6 |
| A7 | — | — | toast limite d'onglets : spécialiste B (`context-toast.h`) | — |
| A8 `FKR-0` | `FRN-0`, `FQS-0`, `FT3-0`, `FNT-0`, note `FVF-0` | `AboutSpec`, `AboutStatus` (`:1410-1429`) | tuile 48 raised + fleur 28 `#DFDFDF`, titre 20/24, version tabulaire, ligne d'état 48 `#FFFFFF0A` (à jour / prête / impossible / recherche avec spinner à la place de l'icône), liens 32 + chevron, pied moteur, ✕, pile 24 ; focus sur l'action d'état | Titre en Medium (pas de SemiBold) |
| A9 `FWX-0` | `G3X-0`, notes `G8R-0`, `G8N-0`, chip `G5O-0` | `PrivateSpec(false)` / `PrivateSpec(true)` (`:1430`) | « Navigation privée indisponible », « Aucun onglet n'a été ouvert. », « Effacer les données… » discret + « Compris » ; concept futur = même modale badgée Démonstration résumant `G8N-0` | La maquette « Fenêtre privée » du concept n'est pas construite (Paper : « ne pas livrer ») |

## 4. Popovers d'origine B01–B08 (`src/origin-prompt.h`)

Un seul rendu (`Modal::Dialog`), deux placements : `Scope::Anchor` (sans voile, rejet léger au
clic extérieur) et `Scope::Page` (400 px centré dans la page, voile 40 %, bloque l'onglet seul,
note `FD4-0`).

| Artboard | Nœuds lus | `Kind` | Valeurs |
| --- | --- | --- | --- |
| B01 `DEZ-0` | `DMK-0`, `DGD-0` (positions) | Permission groupée | popover 340, gauche = indicateur − 4 DIP (bord du champ d'adresse), haut = bas de l'indicateur + 16 DIP (Paper x 82 / y 88 vs champ 44–76) |
| B02 `DQ5-0` | `DZF-0`, `DSW-0`, `DTI-0`, `find_nodes DQY-0`, note `DQD-0` | Permission | titre 16/20 Medium + sous-titre 14/18 60 % + icône 16 (70 %) dans 24 ; ✕ 14 `#FFFFFF82` ; lignes 32 (écart 4) icône + libellé 16/20 + « Approximative » ; case « Mémoriser pour ce site » (16, bord 1,5 `#FFFFFF66`, r 4) ; notifications : ligne info « Choix toujours mémorisé… » (mémorisé d'office) ; Bloquer/Autoriser étirés ; textes position/notifications/presse-papiers verbatim |
| B03/B04 `EDA-0`, `EOL-0` | `EPC-0`, `EV0-0` | PasswordSave/Update/Fill | 360 ; libellés 14/18 70 % écart 6, champs raised, œil 16 (révéler), « Jamais pour ce site » en tête, Plus tard / Enregistrer (padding 14) ; liste : padding 6, en-tête 14/18 Medium 50 %, lignes 48 avatar 24, filet, « Gérer les mots de passe » ; ↓ puis Entrée ; identifiant manquant invalide |
| B05 `EX3-0` | `find_nodes EX3-0`, notes `F9N/F9O/F9P` | `password-demo.h` | voir §5 |
| B06 `FBS-0` | `FEE-0`, `FJP-0`, `find_nodes FJ4-0`, notes `FNN/FNO/FNP` | BasicAuth, ExternalApp | 400 centré ; titre 20/24 −0,02em ; encadré cadenas `#FFFFFF0A` (HTTP) ; Identifiant/Mot de passe (ES_PASSWORD) ; refus : erreur 14/18 sous le mot de passe et focus dessus (`AuthRefused`) ; appli : tuile 32 + « Ouvrir Zoom Workplace ? », « Toujours autoriser ce site à ouvrir les liens zoommtg » ; cadre tiers : texte « Un contenu intégré à … », info, Annuler par défaut, aucune mémorisation |
| B07 `FO6-0` | `FU2-0` | DangerousDownload | 360 ancré à droite (bouton Téléchargements), carte danger 8 % + anneau 20 %, tuile triangle 15 %, « Bloqué : fichier dangereux », Supprimer le fichier / Plus d'options (menu hôte : Copier le lien, Signaler, Conserver quand même… ⇒ Keep) |
| B08 `FVC-0` | `find_nodes FVC-0` | ClientCertificate, ScreenShare, KeepFile | « Choisir un certificat » (liste, Ne pas envoyer) ; « Partager avec … » (Onglet/Fenêtre/Écran entier, badge Démonstration : le sélecteur du moteur ne se remplace pas) ; « Conserver ce fichier dangereux ? » (Annuler par défaut) |

**Écarts assumés (B)** : pas de liste déroulante d'appareil caméra/micro (WebView2 ne
l'expose pas) ; certificat expiré non grisé (le modèle de liste n'a pas d'état désactivé) ;
« Plus d'options » non étiré dans Paper mais étiré ici (rangée étirée commune) ; mise en
valeur Medium E6 de l'hôte dans les phrases B06 non reproduite (texte d'un seul style) ;
confirmation « Enregistré » 4 s (B04 `EWO-0`) non construite — elle relève d'un toast (B).
Les variantes mots de passe et B08 sont des **démonstrations** (badge) : aucune donnée réelle.

## 5. B05 démonstration (`src/password-demo.h`)

Paper : sous-vue 332 px du panneau Réglages. Ici : surface autonome 332 px (Modal::Dialog,
voile fenêtre), `settings-panel.h` non modifié. Liste « Enregistrés · N » → détail
(Identifiant, Mot de passe masqué + œil, « Utilisé sur … · Modifié le … », « Supprimer ce mot
de passe… ») → confirmation dans la surface (carte danger, focus sur « Supprimer », note
`F9P-0`). Cinq comptes **fictifs en mémoire** (ceux de Paper, l'identifiant au format NIR
d'ameli remplacé par `0 00 00 00 000 000`), mots de passe effacés (`SecureZeroMemory`) à la
suppression et au `Dispose`. Rien n'est stocké, rien n'est lu d'une page.
Écarts : pas de recherche ni de groupe « Jamais enregistrés » ; pas de Windows Hello avant
« Afficher » (note `F9O-0` : fenêtre système, hors démo) ; pas de masquage auto après 30 s.

## 6. B09 — note de faisabilité (`G00-0`, aucune UI)

* **Mots de passe (B03–B05) — Chantier dédié** : le SDK ne permet pas de redessiner l'invite
  native ni sa liste ; il faut couper l'enregistrement natif, détecter formulaires et
  soumissions par script injecté, stocker chiffré côté Windows, dessiner soi-même. D'où le
  statut Démonstration ici.
* **Windows Hello — Reste système** : vérification avant d'afficher/copier et passkeys /
  WebAuthn restent la fenêtre Windows, jamais imitée.
* **Autorisations (B01, B02) — Faisable** : `PermissionRequested` avec report ; mémorisation
  stockée par le navigateur ; refus Windows (confidentialité caméra/micro) détecté, renvoi vers
  les paramètres Windows.
* **Auth HTTP, application externe, certificat, téléchargement (B06–B08) — Faisable** :
  `BasicAuthenticationRequested`, `LaunchingExternalUriScheme`, `ClientCertificateRequested`,
  `DownloadStarting` + verdict SmartScreen (affiché, jamais recalculé).
* **Partage d'écran (B08) — À vérifier** : dépend du SDK ; sinon garder le sélecteur intégré et
  ne dessiner que le bandeau « partage en cours ».

## 7. Tests

```powershell
./test-popups.ps1              # en-têtes seuls + avec chrome.h (0 warning), modal_test, popup_shadow_test
./test-popups.ps1 -SkipPixels  # sans les contrôles pixel (bureau verrouillé / CI)
```

Résultats du 2026-11-09 : en-têtes OK ; `modal_test` **77/77** ; `popup_shadow_test` 27
contrôles structurels OK, 8 contrôles pixel **NOT VERIFIED** (session verrouillée pendant
l'exécution). Non-régression : `test-downloads.ps1`, `test-history.ps1`, `test-ui.ps1` passent ;
`test-bookmarks-ui.ps1` échoue sur l'assertion de l'ancien contrat « arrondi DWM »
(`tests/bookmarks/bookmarks_ui_test.cpp:257`, hors de ma propriété — correctif transmis au lead).
Rendus hors écran (WIC, `Dialog::RenderForTest`) comparés aux captures Paper : A5, A8, B02,
B06, B07 conformes (fichiers `build/popups/shots/*.png`, non versionnés).
