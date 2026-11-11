# Raccordements réels — plan, matrice et limites (branche VALXXII/Design-missing-elements)

État vivant, mis à jour à chaque lot. SDK WebView2 **1.0.4314.35** (`deps/webview2`), docs Microsoft Learn
(`?view=webview2-1.0.4314.35`, mises à jour le 2026-10-19). Base : `origin/main` 599da56 (0.3.132, #58, #60) fusionnée en `ac81314`.
PR61 (tooltip d'onglet = titre seul) reprise par cherry-pick `df97274` (même patch, pas de fusion annoncée).

Légende état : **FAIT** · **TRANSITOIRE** (comportement honnête provisoire, pas la fin du mandat) · **À FAIRE** · **LIMITE API** · **SANDBOX**.
Légende preuve : **U** = test unitaire / intégration sur faux arguments moteur ou rendu hors écran (aucun WebView2 réel) ;
**E2E-sim** = hôte et moteur de mise à jour réels, transport simulé (`tests/update-integration`) ; **E2E** = navigateur réel
contre une fixture réelle. Aucune ligne U n'est présentée comme un E2E.

## Décisions du coordinateur appliquées
1. Mots de passe : option A pour les nouveaux enregistrements, coffre actif seulement complet **et** après revue sécurité
   indépendante ; autosave moteur coupé seulement alors ; anciens secrets/passkeys laissés au moteur.
2. Fenêtres : A renforcée (un processus par fenêtre, magasin transactionnel interprocessus, tests adversariaux 2 processus).
3. Directions E1–E8 / D1–D6 (`coverage-decisions.md`), C7, A6 420, copies A8/B07 définitives, décisions 4/7/11
   (`contract-decisions-4-7-11.md`), revue indépendante F1–F22 + R1.

## Matrice composant → événement réel → effet → état → preuve
| # | Composant (Paper) | API / événement réel (Learn) | Effet observable | État (SHA) | Preuve |
|---|---|---|---|---|---|
| 3 | B08 certificat client | [`ICoreWebView2_5::add_ClientCertificateRequested`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2_5?view=webview2-1.0.4314.35#add_clientcertificaterequested), [`…RequestedEventArgs`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2clientcertificaterequestedeventargs?view=webview2-1.0.4314.35) | Vrais candidats (émetteur, expiration ; expirés désactivés). **E1** : la ligne/flèche/Espace sélectionne seulement, « Envoyer » (désactivé sans choix valide) transmet ; « Ne pas envoyer »/Échap = sans certificat ; navigation/onglet = Cancel ; deferral 1× | FAIT (9cdb63a, cb4bf44) | U `elements_integration_test` (faux args) ; **E2E-web mTLS PASS** `tests/e2e/mtls_e2e.ps1` (3/3) : PKI fixture épinglée dans l'hôte de test seulement, vrai `ClientCertificateRequested` (1 candidat = certificat du run), « Envoyer » désactivé avant choix, sélection n'envoie rien, **identité observée par le serveur** après « Envoyer », « Ne pas envoyer » = aucune identité, certificat + clé du run supprimés et vérifiés, `CurrentUser\My` identique à avant |
| 2 | B07 téléchargement bloqué | [`ICoreWebView2DownloadOperation::get_InterruptReason`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2downloadoperation?view=webview2-1.0.4314.35#get_interruptreason) FILE_MALICIOUS / FILE_SECURITY_CHECK_FAILED / FILE_BLOCKED_BY_POLICY | Titre de carte + texte **par raison** (copies validées) ; actions par raison (décision 4) : MALICIOUS = Plus d'options (Copier le lien, Signaler → vraie page `microsoft.com/wdsi/filesubmission` sur clic) ; CHECK_FAILED / POLICY = « Copier le lien » direct ; Supprimer pour les 3 ; **R1** raison complète en UIA (description, groupe carte, aide des boutons) | FAIT (c145ba8, 5ccc916, 1ad6ea6) | U (copie, actions par raison, aucune page de signalement hors MALICIOUS, nœuds UIA R1) ; **E2E-web INCONCLUSIF** `tests/e2e/b07_download_e2e.ps1` (fichier de démonstration SmartScreen officiel démarré, jamais interrompu en 90 s ; aucune API de réputation dans le SDK) — journal `design/e2e-runs/ecaf54e/b07_download_e2e.txt` |
| 2 | Ligne de liste bloquée (E6) | idem | « Bloqué · fichier dangereux / vérification de sécurité impossible / règle de sécurité » | FAIT (cb4bf44) | U |
| 2 | B08 « Conserver quand même » | — | — | LIMITE API : ni type de danger ni override dans `ICoreWebView2DownloadOperation` | SANDBOX seulement |
| 8 | A8 À propos | `UpdateCore::Engine::View()`, `LastCheckResult()`, `LastFailure()` (lecture seule, ajouté), `UpdateAction` | Rangée d'état 48 min, +padding 6 si détail sur 2 lignes. Idle sans résultat positif = « Mises à jour » ; « browser est à jour » seulement après UpToDate ; Checking ; Disponible → Télécharger ; Téléchargement → **Annuler = Action::Cancel** ; Prête → Redémarrer (#60) ; Installation sans bouton ; échec vérif = « Vérification impossible » + « La recherche de mises à jour a échoué. Réessayez. » ; échec téléchargement = « Échec du téléchargement » + message moteur + **Réessayer = Retry réel** (re-télécharge) ; échec installation = « La mise à jour a échoué » + message moteur + « Rechercher à nouveau » (= Check). Pied Réglages : « Vérif. impossible », jamais « Hors ligne » deviné | FAIT (048b864, 5ccc916, deabb27, 9ce8dfb) | E2E-sim `host_e2e` (Checking, progression + Annuler, Télécharger, Redémarrer → closeWithoutPrompt, échec de vérification) ; U `modal_test` (48→68, bouton centré, version longue, 96/144/192) |
| 9 | A2 dialogues répétés | [`add_ScriptDialogOpening`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2?view=webview2-1.0.4314.35#add_scriptdialogopening) | Case dès le 2e dialogue ; cochée = suivants refusés ; levée à la navigation | FAIT (2743eae) | U |
| 9 | A4 beforeunload actualiser | idem, BEFOREUNLOAD + notre Reload | « Actualiser ce site ? » | FAIT | U |
| 9 | A6 fermeture (420, EY9-0, fil c-npdt3g) | `WM_CLOSE` → `CloseWindowAllowed` | 420 DIP pour cette confirmation seule ; touches Ctrl Maj T ; « Ne plus demander » sur la rangée des boutons (repli au-dessus si trop long) ; variante téléchargement (fichier réel, `FormatSize` 1 décimale, total inconnu = reçu seul) ; **F4** fenêtre privée : « Les onglets d'une fenêtre privée ne peuvent pas être rouverts. », sans case | FAIT (5ccc916, 9ce8dfb) | U `modal_test` (420, géométrie, nom long, rendus 96/144/192 vs Paper EY9-0) + intégration |
| 9 | A6 suppression favoris, A3 InputSpec | — | — | SANDBOX volontaire | — |
| C7 | Plein écran | `add_ContainsFullScreenElementChanged`, F11, Échap | Page : pastille « <origine réelle> est en plein écran · Échap » ; F11 navigateur : pastille « Plein écran · F11 », chrome masqué. Bouton « Quitter le plein écran » / révélation du chrome : **non livrés** (aucun contrôle inerte affiché) ; clavier verrouillé = sandbox (aucun signal SDK) | FAIT (5ccc916) | U `debug_sandbox_test` (rendu) ; pas d'E2E fenêtre réelle |
| 6 | A9 fenêtre privée | [`ICoreWebView2Environment10::CreateCoreWebView2ControllerOptions`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2environment10?view=webview2-1.0.4314.35), [`ICoreWebView2ControllerOptions::put_IsInPrivateModeEnabled` / `put_ProfileName`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2controlleroptions?view=webview2-1.0.4314.35) | Processus séparé `--private` (bouton P, Ctrl+Maj+N), contrôleurs InPrivate profil `browser-private` ; aucune trace automatique écrite (session, historique, favicons, liste de téléchargements, réglages, log) ; historique non lu (point 8) ; fichiers téléchargés restent sur le disque ; **puce « Privée »** non interactive (décision 7, LRZ-0), P = commande normale, UIA Text, titre OS « (navigation privée) » | FAIT (3c57a2e, 9ce8dfb, b09bdfe, f521c6e) | U (aucun fichier écrit, ligne de commande, profil dédié, rendu puce) ; **E2E-app** `tests/e2e/windows_e2e.py` (processus `--private` réel : aucun fichier app, aucune session, titre) ; **E2E-web** `tests/e2e/private_isolation_e2e.ps1` PASS sur runtime Fixed Version (cookies et localStorage isolés dans les deux sens, nouvelle fenêtre privée vide) |
| 6 | D3 favoris volontaires en privé | magasin transactionnel (`src/profile-store.h`) | Un favori ajouté/modifié/supprimé depuis une fenêtre privée passe par la transaction normale ; aucune trace d'historique, session, réglages ou log ; « Ne plus demander » absent en privé (F4). Réglages modifiés en privé : non persistés (TRANSITOIRE, à brancher sur la même transaction) | FAIT favoris (2b877bf) ; réglages TRANSITOIRE | U `elements_integration_test` (écrit via transaction, rien d'autre) |
| 7 | A7 limite 128 → Nouvelle fenêtre, D4, sessions par fenêtre | `src/window-sessions.h` (mutex nommé par fenêtre, adoption sous verrou profil, réservations) | Vraie fenêtre normale avec la page refusée ; URLs écrites avant lancement (ack durable = claim du nouveau processus) ; échec → « Impossible d'ouvrir une nouvelle fenêtre » + Réessayer sans doublon, URLs gardées (orpheline restaurée) ; restauration >128 → reste proposé ; chaque fenêtre restaurée avec ses onglets ; fermer une fenêtre parmi d'autres ne la ressuscite pas ; dernière/crash/#60 conservées | FAIT (6a1a4c1, f521c6e) | **E2E-store** `test-profile-store` (2 démarrages simultanés, doublon, tué avant sauvegarde, restauration off) ; **E2E-app** `windows_e2e.py` PASS (adoption + relance, fermeture parmi d'autres, dernière, crash) ; A7 via toast : U seulement |
| 1 | B03/B04/B05 mots de passe (D1, D2, E8) | aucune API coffre moteur ; DPAPI [`CryptProtectData`](https://learn.microsoft.com/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata) ; [`put_IsPasswordAutosaveEnabled`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2settings4?view=webview2-1.0.4314.35) | Cœur : DPAPI lié à l'origine https, fichier sans clair, ids aléatoires, fusion interprocessus, gardes cadre/document/origine/action, script de remplissage échappé. **Inactif** (`Vault::Active()` faux) : moteur natif conservé. Branchement B04/B03/B05 réel + E8 : après revue | CŒUR FAIT (621eb18) ; branchement À FAIRE après revue | U `test-password-vault` |
| 1b | Suggestions identifiant/passkey du moteur | aucune API WebAuthn/passkey (0 occurrence dans `WebView2.h`) | Popup moteur inchangé | LIMITE API | — |
| 4 | B08 partage d'écran | [`ICoreWebView2ScreenCaptureStartingEventArgs`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2screencapturestartingeventargs?view=webview2-1.0.4314.35) (Cancel, Handled, OriginalSourceFrameInfo, GetDeferral) | Sélecteur du moteur conservé ; notre sélecteur = sandbox | LIMITE API | Learn |
| 5 | Voyant caméra/micro « en cours » (D5) | aucun signal d'usage | Autorisé/bloqué réels ; « en cours » = démo ; aucune heuristique injectée | LIMITE API | header |
| D6 | Autoriser une fois / Mémoriser | [`ICoreWebView2PermissionRequestedEventArgs3::put_SavesInProfile`](https://learn.microsoft.com/microsoft-edge/webview2/reference/win32/icorewebview2permissionrequestedeventargs3?view=webview2-1.0.4314.35) | « Mémoriser » décoché / Autoriser une fois = non mémorisé ; aucune cause Windows affichée | FAIT (existant) | U |

## Stores interprocessus (décision 2) — état
| Fichier partagé | Transaction | Fusion | Preuve |
|---|---|---|---|
| favoris | `ProfileStore::Commit` | `Bookmarks::Store::Merge` (3 voies, ids remappés, aucune résurrection, édition jamais appliquée à un id réutilisé) | E2E-store 2 processus + lecteur (0 fichier tronqué), crash du détenteur ; U favoris/UI |
| historique (barre d'adresse) | `CommitWith` | `History::Merge` (visites en delta, suppressions respectées) | U |
| réglages | `CommitWith` | champ par champ (`MergeKeyValues`) | U |
| téléchargements | verrou + `WriteAtomic` | `DL::MergeHistory` (clé fichier+début+source, lignes en cours intactes) | U |
| sessions | un fichier par fenêtre | adoption/réservation sous verrou | E2E-store + E2E-app |

## E2E web : runtime Microsoft Fixed Version (fixture uniquement)
Le runtime système échoue (`CreateEnvironment` 0x80070002, lot startup séparé, non touché). Pour NOS tests seulement :
`Microsoft.WebView2.FixedVersionRuntime.157.0.4341.46.x64.cab`, URL complète publiée par https://developer.microsoft.com/en-us/microsoft-edge/webview2/ (section Fixed Version) :
`https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/edb19202-9173-4dd1-ab87-c45c2d80aaec/Microsoft.WebView2.FixedVersionRuntime.157.0.4341.46.x64.cab`
(310 997 156 octets), SHA-256 `0AC7317A08C3F3BEEBBB9B6D90F5B77352569F0A33A54A15EF6AAABC82899B7C`,
Authenticode valide « Microsoft Corporation » (CAB et `msedgewebview2.exe`), extrait par `expand -F:*` dans `build/webview2-fixed/` (ignoré),
ACL documentées par Microsoft pour le mode Fixed Version ([Distribute your app and the WebView2 Runtime › Fixed Version](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution#details-about-the-fixed-version-runtime-distribution-mode) :
`icacls <dossier> /grant *S-1-15-2-2:(OI)(CI)(RX)` et `*S-1-15-2-1:(OI)(CI)(RX)`) sur ce seul dossier (`/T`, aucun parent modifié),
donné aux processus de test par `WEBVIEW2_BROWSER_EXECUTABLE_FOLDER` (environnement du seul script). Rien d'installé, enregistré ni embarqué ;
ne vaut pas validation de l'installation Evergreen. Commandes : `curl -L -o build/webview2-fixed/<cab> <URL>` ; `Get-AuthenticodeSignature`
(Valid, CN=Microsoft Corporation) ; `Get-FileHash -Algorithm SHA256` ; `expand.exe <cab> -F:* build/webview2-fixed/157.0.4341.46`. Nettoyage :
supprimer `build/webview2-fixed` et `build/e2e-windows` (fixtures), les scripts ne tuent que les `browser.exe` dont l'image est dans la fixture. Résultats : isolation privée **PASS** ; B07 réel **INCONCLUSIF** (le moteur démarre le
fichier de démonstration SmartScreen officiel sans l'interrompre en 90 s ; aucune API de réputation dans le SDK) ; mTLS : première sonde (17e0892) bloquée avant la demande client ; puis, avec l'autorisation ciblée, **PASS** via un épinglage test-only (voir ci-dessous). Sonde initiale
(serveur local `tests/e2e/mtls_server.py`, certificat serveur jetable, CertificateRequest limité à la CA du run, runtime Fixed Version) : le
moteur rejette le certificat serveur non approuvé (`CERTIFICATE error`, `NAVIGATION FAILED WebErrorStatus=5`) **avant** toute demande de
certificat client ; `ClientCertificateRequested` n'est donc jamais atteint. Le débloquer exigerait d'approuver ce certificat serveur
(magasin de confiance ou contournement TLS), hors autorisation : aucun certificat ni clé créé dans CurrentUser\My (vérifié : 0), sonde supprimée.

## Besoins UI (rédacteur Paper unique ; aucune mutation par l'intégrateur)
- B05 : état vide et note de cohabitation (E8) ; PW-FILL-V4 conditionnel (décision 11).
- Aucun autre écran demandé ; toutes les géométries ci-dessus viennent des nœuds Paper lus (EY9-0, LRZ-0, FKR-0, FO6-0…).

## Revue indépendante obligatoire avant activation du coffre / PR finale
Coffre (DPAPI, origine/cadre/document, consentement, aucun secret via le pont ni dans les logs) et fenêtres/privé
(isolation, transactions interprocessus). Aucun merge, aucune publication.

## mTLS E2E (autorisation ciblée du coordinateur) — épinglage fixture test-only
- Hook `src/e2e-fixture-pin.h`, compilé **uniquement** dans `tests/e2e/mtls_host.cpp` (`BROWSER_E2E_FIXTURE_HOOK`) ; absent du binaire dev/release
  (vérifié par recherche du marqueur dans `build/real-integration-check/browser.exe`). `ALWAYS_ALLOW` via l'API supportée
  `ServerCertificateErrorDetected` seulement pour `https://127.0.0.1:<port fixture>`, statut `CERTIFICATE_IS_INVALID` (statut générique de WebView2 :
  la vraie borne est l'empreinte + le SAN + la validité), certificat valide, SAN = IP 127.0.0.1 exactement, SHA-256 du DER = manifeste du harness.
  Matrice négative U `test-e2e-pin.ps1` (20 cas : empreinte, hôte, port, schéma, statuts, validité, SAN DNS/supplémentaire/absent, formes d'URI
  limites, manifeste absent) ; échec si le marqueur apparaît dans le binaire dev/release. Ceci épingle une PKI
  de fixture : ce n'est ni une validation de PKI de production ni de l'installation Evergreen.
- Cache `ALWAYS_ALLOW` (hôte+certificat, session du processus moteur) : l'hôte de test refuse **avant envoi** toute navigation (principale,
  cadre, redirection) et toute requête hors `https://127.0.0.1:<port du manifeste>` (ecaf54e), efface les décisions de certificat au démarrage
  (`ClearServerCertificateErrorActions`), échoue fermé si une garde manque ; profil neuf et fin vérifiée des processus moteur entre étapes.
  Scénario E2E : même certificat servi sur un second port après mise en cache → navigation, redirection et sous-ressource n'atteignent jamais ce serveur.
- Certificat client : `CurrentUser\My` seulement, sujet `CN=browser-e2e-mtls-<run>`, EKU ClientAuth, clé CNG non exportable ;
  manifeste de propriété durable écrit avant création (`build/e2e-mtls/ownership.json`), reprise au lancement suivant, suppression par
  empreinte exacte + `-DeleteKey`, vérification par le fichier de clé exact du run (aucune énumération des clés). Incident documenté : une
  version antérieure du script vérifiait via `certutil -key`, qui a listé le nom d'une clé d'une autre application (nom seulement, sans lecture
  ni usage) ; remplacé par le test ciblé.

## Journaux d'exécution durables
`design/e2e-runs/<SHA>/` : sortie complète de chaque script (sans secret ; le certificat client n'apparaît que par son sujet de run).
- `ecaf54e` (runtime Fixed Version 157.0.4341.46 x64) : `windows_e2e.txt` PASS, `private_isolation_e2e.txt` PASS, `tls_page_e2e.txt` PASS,
  `mtls_e2e.txt` PASS (manifeste de propriété, handshake observé par le serveur, second port refusé, annulation, certificat + clé supprimés,
  magasin identique), `b07_download_e2e.txt` INCONCLUSIF, `unit_store_vault_pin.txt` PASS.
- Rappel : les corrections M1-M3 (coffre v2) sont déclarées et testées par l'intégrateur après la revue (ea096a1) ; leur recontrôle indépendant
  fait partie de la revue finale.
