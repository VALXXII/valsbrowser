# Récupération WebView2 au démarrage — diagnostic, spec, plan

Statut : implémenté sur `VALXXII/webview2-startup-recovery` (base `origin/main` 599da56), corrigé après la revue indépendante 304775b (§7).

## 1. Symptôme

Au démarrage, fenêtre fatale « WebView2 — arrêt demandé / CreateEnvironment : 0x80070002 /
Opérations arrêtées. Aucun correctif automatique. », puis un navigateur vide et inutilisable.
`browser.log` : des démarrages `RUNTIME 157.0.4341.46`, puis `WEBVIEW2 ERROR CreateEnvironment : 0x80070002`.

## 2. Diagnostic (lecture seule, 11 nov., PC de l'utilisateur, aucune mutation)

| Observation | Valeur |
|---|---|
| `pv` du runtime (HKLM WOW6432Node, HKLM, HKCU, GUID `{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}`) | absents |
| `HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate` (toute la clé), dossier `Program Files (x86)\Microsoft\EdgeUpdate`, service `edgeupdate*`, tâches `MicrosoftEdgeUpdate*` | absents |
| Navigateur Edge (`Program Files (x86)\Microsoft\Edge\Application`) | absent |
| `Program Files (x86)\Microsoft\EdgeWebView\Application\157.0.4341.46` | **16 fichiers seulement** (msedge.dll, msedgewebview2.exe, EmbeddedBrowserWebView.dll, d3dcompiler…) ; pas de manifest, `Locales`, `resources.pak` |
| Policies `Software\Policies\Microsoft\Edge\WebView2` (HKLM, HKCU) ; variables `WEBVIEW2_*` | aucune |
| `GetAvailableCoreWebView2BrowserVersionString(null)` via le loader livré 0.3.132 | `0x80070002`, version nulle |

Lecture : `0x80070002` = `HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)`, documenté pour
`CreateCoreWebView2EnvironmentWithOptions` comme « Could not find Edge installation » et pour
`GetAvailableCoreWebView2BrowserVersionString` comme « fails to find an installed WebView2 runtime ».
Le loader et l'appel sont atteints : ce n'est ni une DLL loader manquante, ni le profil.

Hypothèse la plus probable (non prouvée) : une désinstallation d'Edge + Edge Update + runtime
(outil tiers ou manuel) a supprimé l'enregistrement et presque tous les fichiers, laissant les
fichiers alors verrouillés. Les 16 fichiers restants ne forment pas un runtime utilisable : leur
seule présence ne prouve rien. La cause de la « disparition après reboot » n'est **pas établie**.
Une réinstallation Evergreen par le bootstrapper officiel réinstalle Edge Update et le runtime, ce
qui est le remède documenté ; ce correctif ne prétend pas réparer le PC sans cette installation.

## 3. Sources primaires

- Distribution : <https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution>
  — « Before your app creates a WebView2, the app should check whether the WebView2 Runtime is
  present (either by checking a registry key or by calling an API) and install the Runtime if it
  is missing » ; détection (*Approach 2* : `GetAvailableCoreWebView2BrowserVersionString`,
  `versionInfo == nullptr` ⇒ absent) ; bootstrapper « Get the Link » ; `MicrosoftEdgeWebview2Setup.exe
  /silent /install` ; non élevé ⇒ *per-user*, remplacé par *per-machine* si un Edge Updater
  machine existe.
- Référence Win32 (SDK présent 1.0.4314.35) :
  <https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/webview2-idl?view=webview2-1.0.4314.35>
  — table d'erreurs de `CreateCoreWebView2EnvironmentWithOptions` (`ERROR_FILE_NOT_FOUND`,
  `ERROR_PRODUCT_UNINSTALLED`, `ERROR_NOT_SUPPORTED`, `ERROR_INVALID_STATE`, `ERROR_DISK_FULL`,
  `ERROR_FILE_EXISTS`, `E_ACCESSDENIED`, `E_FAIL`) ; overrides `WEBVIEW2_BROWSER_EXECUTABLE_FOLDER`,
  `WEBVIEW2_CHANNEL_SEARCH_KIND`, `WEBVIEW2_RELEASE_CHANNELS`, policies
  `[HKLM|HKCU]\Software\Policies\Microsoft\Edge\WebView2\{BrowserExecutableFolder,ChannelSearchKind,ReleaseChannels}`
  (nom de valeur = AUMID, puis nom de l'exe, puis `*`) ;
  `GetAvailableCoreWebView2BrowserVersionStringWithOptions` applique les mêmes overrides et options.
- Lien du bootstrapper : <https://developer.microsoft.com/en-us/microsoft-edge/webview2/> — le
  bouton « Get the Link » (code de la page, vérifié le 11 nov.) ouvre
  `https://go.microsoft.com/fwlink/p/?LinkId=2124703`, qui répond `301` vers
  `https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/<guid>/MicrosoftEdgeWebview2Setup.exe`.
  Fichier téléchargé (sans exécution) : 1 897 688 octets, Authenticode **Valid**, signataire
  `CN=Microsoft Corporation, O=Microsoft Corporation`, émetteur `Microsoft Code Signing PCA 2024`,
  racine `Microsoft Root Certificate Authority 2011`, produit « Microsoft Edge Update » 1.3.315.17.
- Lien manuel pour l'utilisateur : <https://developer.microsoft.com/microsoft-edge/webview2/consumer/>
  (cité par la page Distribution).
- Confiance : `WinVerifyTrust` (<https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust>),
  `WTHelperProvDataFromStateData` / `WTHelperGetProvSignerFromChain`,
  `CertVerifyCertificateChainPolicy` + `CERT_CHAIN_POLICY_MICROSOFT_ROOT`
  (<https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certverifycertificatechainpolicy>).
- Processus : `CreateProcessW` (<https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw>) —
  `lpApplicationName` explicite, pas de shell, pas d'héritage de handles ; `ERROR_ELEVATION_REQUIRED`
  si le binaire exigeait une élévation (on n'élève jamais).

## 4. Comportement

1. **Démarrage normal** : `LoadLibraryEx(WebView2Loader.dll)` → sonde
   `GetAvailableCoreWebView2BrowserVersionStringWithOptions(null, mêmes EngineOptions)` →
   `CreateCoreWebView2EnvironmentWithOptions`. Runtime présent : aucun réseau, aucune UI en plus
   (un appel de sonde ; log `WEBVIEW2 PROBE version=…`).
2. **Classification** (`src/webview2-runtime.h`, pure) par étape + HRESULT + résultat de sonde + overrides :
   - loader absent / export absent → *paquet incomplet* : pas d'installation, Réessayer / Quitter ;
   - sonde « introuvable » sans override → *runtime absent* : **Installer WebView2** proposé ;
   - sonde « introuvable » avec override env/policy (dossier, canal) → *override invalide* : on
     explique quelle variable/policy agit, on ne la modifie pas, pas d'installation ;
   - sonde OK mais `CreateEnvironment` = `ERROR_FILE_NOT_FOUND`/`ERROR_PRODUCT_UNINSTALLED` →
     *découverte incohérente* : installation proposée (même famille documentée, même appel) ;
   - `E_ACCESSDENIED`/`ERROR_FILE_EXISTS` → profil inaccessible ; `ERROR_INVALID_STATE` → options
     différentes d'une instance déjà ouverte ; `ERROR_NOT_SUPPORTED` → dossier override invalide ;
     `ERROR_DISK_FULL`, `E_FAIL`, autre → message + Réessayer, jamais d'installation.
   Un `0x80070002` ailleurs (autre étape) ne déclenche jamais l'installation.
3. **UI** : le `Modal::Dialog` natif existant (Direct2D, sans WebView2, portée fenêtre), en
   français : titre, explication, ligne technique (étape, code, version), lien « Page Microsoft
   WebView2 », boutons **Installer WebView2** / **Réessayer** / **Quitter**. Pas de nouvelle maquette.
4. **Installation** (`src/webview2-install.h`) après clic seulement, une seule à la fois dans ce
   processus (boutons verrouillés, double clic ignoré) **et entre processus du même utilisateur** :
   - téléchargement WinHTTP (mêmes limites par opération que le transport de l'updater : proxy
     système, délais 10/10/15/20 s, TLS 1.2/1.3 sans relâchement, ≤ 6 redirections manuelles —
     mais un objet distinct : la politique GitHub de l'updater reste intacte) depuis l'URL exacte
     ci-dessus ; redirections acceptées seulement en HTTPS:443, sans identifiants, hôte
     `microsoft.com` ou `*.microsoft.com` ; taille ≤ 16 Mio ; **échéance totale 5 min** (contre
     un serveur au compte-gouttes ; une lecture bloquée peut la dépasser d'au plus son délai de
     20 s). Redirections automatiques, cookies et authentification sont coupés au niveau session
     (`WINHTTP_OPTION_REDIRECT_POLICY = NEVER`) et requête (`WINHTTP_OPTION_DISABLE_FEATURE`) ;
     **si l'une de ces options ne peut pas être posée, la requête n'est pas envoyée** (échec fermé) ;
   - fichier écrit dans un dossier neuf au nom aléatoire (`RtlGenRandom` = `SystemFunction036`,
     le CSPRNG de `rand_s`) sous `%TEMP%`, DACL protégée (utilisateur courant + SYSTEM),
     `CREATE_NEW` ; rouvert en lecture avec partage lecture seule pendant la vérification **et**
     l'exécution (pas d'échange de fichier possible) ;
   - `WinVerifyTrust` sur ce handle (`WINTRUST_ACTION_GENERIC_VERIFY_V2`, révocation de toute la
     chaîne ; seul le retour `0` vaut confiance), puis la chaîne du signataire
     (`WTHelperGetProvSignerFromChain`) doit satisfaire `CERT_CHAIN_POLICY_MICROSOFT_ROOT` **et** le
     certificat signataire doit être `CN=Microsoft Corporation`, `O=Microsoft Corporation` **et** la
     ressource de version doit nommer `MicrosoftEdgeUpdateSetup.exe`. Cette ressource est lue
     **depuis l'image verrouillée elle-même** (mappage en lecture seule du handle, parcours
     PE → `RT_VERSION` → `OriginalFilename`), jamais par chemin : aucune redirection MUI
     (`<langue>\<nom>.exe.mui`) ne peut la substituer, et la section ressource fait partie de
     l'image hachée par Authenticode. Le sujet seul ne vaut jamais attestation : il n'est lu qu'après
     la validation cryptographique et la racine Microsoft.
     **Mesure** : avec `dwFlags = 0`, la politique `MICROSOFT_ROOT` *refuse* la chaîne du vrai
     bootstrapper (racine « Microsoft Root Certificate Authority 2011 ») ; elle l'accepte avec
     `MICROSOFT_ROOT_CERT_CHAIN_POLICY_CHECK_APPLICATION_ROOT_FLAG`, que la doc décrit comme la
     vérification de « the Microsoft application root "Microsoft Root Certificate Authority 2011" ».
     Le code essaie donc les deux ; aucune racine tierce n'est acceptée.
     **Ce que cela prouve, et pas plus** : « programme d'installation Microsoft Edge Update signé
     par Microsoft, servi par le lien officiel ». `OriginalFilename` est commun à tous les
     bootstrappers Edge Update (navigateur Edge, canaux, WebView2) ; le produit installé est porté
     par un *tag* hors du hash Authenticode. Le fait que ce soit **WebView2** repose sur le lien
     officiel servi en TLS strict par Microsoft, pas sur la signature ;
   - **lancement suspendu puis revendication** : `CreateProcessW(chemin, "\"chemin\" /silent /install",
     CREATE_NO_WINDOW | CREATE_SUSPENDED)`, pas d'héritage, pas d'élévation. Tant que le processus
     est suspendu, aucune instruction ne s'y est exécutée. Sous un mutex nommé par utilisateur, le
     marqueur `%LOCALAPPDATA%\browser\webview2-install.lock` (`pid`, date de création du
     processus, dossier privé) est écrit ; s'il désigne déjà un installateur **vivant** (PID +
     date de création identiques : un PID recyclé ne compte pas), la nouvelle instance est
     terminée sans avoir tourné (« déjà en cours »). Si l'écriture échoue → terminé, rien lancé
     (échec fermé). Puis **dernier contrôle d'annulation**, puis `ResumeThread`. Un installateur
     repris n'est jamais tué ;
   - attente bornée à 10 min ; fermeture pendant l'exécution → issue *Detached* (« le programme
     Microsoft continue en arrière-plan »), distincte de *Cancelled* (« annulée avant que le
     programme Microsoft ne s'exécute : rien n'a été installé ») ;
   - nettoyage : fichier et dossier supprimés à la fin ; s'il tourne encore, le dossier est
     enregistré dans le marqueur et supprimé (lui seul, s'il a la forme `browser-wv2-<24 hex>`)
     par la prochaine vérification après sa sortie. Aucun balayage de `%TEMP%`.
5. **Après l'installateur** (quel que soit le code de sortie) : nouvelle sonde. Introuvable alors
   que l'installateur est allé au bout → corps « l'installation ne l'a pas rétabli », code de
   sortie montré, **plus de bouton Installer** (pas de boucle) ; action principale **Ouvrir la page
   Microsoft**, plus Réessayer et Quitter. Trouvé → nouvelle `CreateEnvironment` ; **seul son
   succès** annonce la récupération et lance `RestoreSession` (une seule fois). Si la création
   échoue encore alors que la sonde voit le runtime : **Redémarrer le navigateur** — la fenêtre
   se ferme normalement (`WM_CLOSE` : sauvegardes, « effacer en quittant » ; en récupération il
   n'y a pas de profil moteur, donc `ClearLocalTraces` s'exécute de façon synchrone), puis,
   **après** la boucle de messages, `RunPendingRelaunch()` relance le même exe avec ses arguments +
   `--webview2-recovered`. La nouvelle instance ne lit donc jamais d'historique pas encore effacé.
   Une instance lancée avec ce drapeau ne propose plus de relance ; chaque relance exige un clic.
6. **Courses** : chaque tentative de création porte une génération ; un rappel périmé ou arrivé
   après la fermeture est ignoré (pas d'écriture vers un HWND détruit). La phase affichée par l'UI
   peut retarder sur le worker (PostMessage) : le contrôleur ne journalise plus de supposition à la
   fermeture ; le **worker journalise lui-même son issue réelle** (`WEBVIEW2 INSTALL WORKER
   outcome=… pid=… microsoft-installer-still-running residue=…`, `Log` sérialisé) ; si le worker
   est encore en vérification après 1,5 s, l'hôte journalise son étape et le dossier qui peut
   rester. Le worker ne peut plus lancer l'installateur après la fermeture (annulation vérifiée
   avant la reprise du processus suspendu).
7. **Logs** : étapes, HRESULT, version, cause, source d'override (nom, pas de valeur complète),
   code de sortie, PID, sujet du signataire, nom du dossier temporaire (`%TEMP%\browser-wv2-…`).
   Jamais d'URL de navigation, de cookie, de contenu de profil ni d'environnement complet.
8. **Tests** : injection par `Effects` (réseau, confiance, lancement, coordination) et par une
   table d'API copiée (WinHTTP simulé sans socket) ; dans `main.cpp`, les points d'injection de
   la fixture (`engineInstallEffects`, `engineCoordination`) n'existent que sous
   `BROWSER_WEBVIEW2_TEST_HOOKS`, défini par `tests/webview2-runtime/host_test.cpp` seulement :
   le binaire du navigateur n'a ni drapeau, ni variable, ni symbole pour remplacer la vérification.

## 5. Plan / fichiers

| Fichier | Rôle |
|---|---|
| `src/webview2-runtime.h` | Classification, lecture des overrides, contrôleur de récupération (machine d'état pure, générations, bornes, textes). Le loader est chargé par `main.cpp` (`PrepareLoader`), pas par ce fichier |
| `src/webview2-install.h` | Politique d'URL, WinHTTP fail-closed + échéance, dossier privé, revendication par utilisateur, vérification Authenticode/éditeur/produit, lancement suspendu, worker annulable. Header-only ; winhttp/wintrust/crypt32 résolus à l'exécution depuis System32 (`LOAD_LIBRARY_SEARCH_SYSTEM32`) : aucune nouvelle dépendance de link. `update-core` n'est **pas** modifié |
| `src/main.cpp` | `wWinMain` → `StartEngine()` ; hôte du contrôleur ; `WM_APP+41` du worker ; `StopEngineStart()` au `WM_DESTROY` ; `RunPendingRelaunch()` après la boucle ; `Log` sérialisé ; `Fail` reste pour les erreurs après démarrage |
| `tests/update-integration/run_all.py` | lance aussi `tests/webview2-runtime/run_tests.py` (aucun changement de `build.py`, du paquet ni du format de mise à jour) |
| `tests/webview2-runtime/` | logique/contrôleur ; installeur (faux réseau, WinHTTP simulé, faux setup lancé pour de vrai, deux processus réels, vraie confiance) ; faux `WebView2Loader.dll` + fixture hôte sur le vrai `main.cpp` (7 scénarios, captures) ; sonde lecture seule du vrai loader |

## 6. Points d'intégration pour le chantier fenêtres privées / processus séparés

- **Init** : `StartEngine()` (appelé dans `wWinMain` après la fenêtre, les surfaces et `StartUpdates()`)
  remplace le bloc `LoadLibraryExW … CreateCoreWebView2EnvironmentWithOptions`. Tout autre
  processus navigateur doit appeler le même `StartEngine()` (ou un `WebView2Runtime::Controller`
  avec les mêmes hooks : mêmes `EngineOptions`, **son propre** dossier de données/profil) et ne
  restaurer sa session que dans `EngineReady()` (hook `ready`, appelé une seule fois).
- **Cleanup** : `StopEngineStart()` au `WM_DESTROY` de chaque fenêtre hôte, `engineStart.reset()`
  avant `FreeLibrary(loader)`, puis `RunPendingRelaunch()` **après** la boucle (hook post-fermeture :
  une fenêtre privée qui l'utilisera devra transmettre son mode / id / session de fenêtre, jamais
  `launchArgs` + `--webview2-recovered` d'une session normale).
- **Unicité de l'installation** : déjà inter-processus (marqueur par utilisateur + mutex), donc
  indépendante du dossier de données, du worktree et du mode privé. Rien à ajouter côté store.
- **Textes** : « Votre profil et vos onglets sont conservés » vaut pour la fenêtre normale ; un
  processus privé devra passer un texte adapté (contrat à reprendre par l'intégrateur).
- **Handoff updater (#60)** : inchangé. `ForwardToInstalled()` reste avant toute fenêtre ;
  `closeWithoutPrompt` est seulement réutilisé pour la relance. Une instance en récupération
  n'écrit pas `session` (`sessionReady` reste faux).

## 7. Revue indépendante 304775b — disposition des constats

| Constat | Disposition | Preuve |
|---|---|---|
| P2-1 annulation / lancement | **Corrigé.** Annulation testée avant `CreateProcessW` ; processus créé suspendu, revendication, dernier test, puis `ResumeThread` ; jamais repris ⇒ terminé (aucun code exécuté) ; repris ⇒ jamais tué, issue *Detached* distincte ; textes et logs selon l'issue réelle du worker | Test du relecteur inchangé : avant `REPRODUCED` ×2, après `NOT REPRODUCED` ×2. `install_test` : annulation déjà posée, annulation pendant suspension, revendication refusée, annulation en cours d'exécution (vivant, identité vérifiable, termine seul) |
| P2-2 deux bootstrappers entre processus | **Corrigé.** Marqueur par utilisateur (PID + date de création + dossier) écrit sous mutex **avant** la reprise ; refus si vivant ; marqueur illisible / processus mort / PID recyclé = périmé, remplacé ; jamais de kill sur la foi d'un marqueur. UI : « déjà en cours », Installer désactivé, Réessayer réévalue | `install_test` : deux vrais processus (le premier « quitte », son faux setup continue ; le second est refusé sans télécharger ; après sortie, nettoyage et nouvelle installation) ; délai dépassé ; marqueurs invalides / PID recyclé ; `host_test install-running` |
| P3-1 `SetOption` non vérifié | **Corrigé** (échec fermé) + `REDIRECT_POLICY_NEVER` vérifié sur la session | WinHTTP simulé : option refusée ⇒ aucun `SendRequest` ; redirection hors Microsoft refusée avant lecture |
| P3-2 ressource de version par chemin (MUI) | **Corrigé** : lecture depuis le mappage du handle verrouillé | Vrai bootstrapper : `MicrosoftEdgeUpdateSetup.exe` ; loader SDK : `WebView2Loader.dll` ; image tronquée : refus |
| P3-3 identité produit partielle | **Documenté** (§4) : « Edge Update signé Microsoft via lien officiel », pas « prouvé WebView2 » | — |
| P3-4 pas d'échéance globale | **Corrigé** : 5 min (injectable) ; statut de chaque phase dit ce que fait Quitter | WinHTTP simulé au compte-gouttes (300 ms injectés) et annulation en cours |
| P3-5 nettoyage après worker détaché | **Corrigé dans la mesure du possible** : issue réelle et résidu journalisés ; dossier d'un installateur encore actif enregistré dans le marqueur et supprimé plus tard, lui seul ; pas de promesse si le processus se termine pendant une vérification longue (journalisé) ; aucun balayage de `%TEMP%` | `install_test` (résidu exact, nettoyage différé, dossier de forme étrangère jamais supprimé) |
| P3-6 relance avant « effacer en quittant » | **Corrigé** : relance après la boucle de messages, donc après `WM_CLOSE`/nettoyage/`WM_DESTROY` | `host_test relaunch` avec `clearOnExit` : rien n'est lancé pendant la fermeture ; l'enfant trouve l'historique déjà effacé (`sentinel=0`) |
| P3-7 crochet de test dans le produit | **Corrigé** : sous `BROWSER_WEBVIEW2_TEST_HOOKS` (fixture seule) | Binaire estampillé : aucune chaîne de la fixture |
| P3-8 doc RtlGenRandom / `Loader` | **Corrigé** (§4, §5) | — |
| P3-9 UX | **Corrigé** : après « installé mais introuvable », plus de bouton Installer, corps honnête, « Ouvrir la page Microsoft » en principal ; pendant l'installation « Il continue même si vous quittez » ; texte privé = contrat d'intégration (§6) | Captures 2, 3, 6 |
| Loader sans export de sonde | Conservé et voulu : `0x80070002` de `CreateEnvironment` est documenté « Could not find Edge installation » ; chemin mort avec le loader livré | `runtime_test` |

## 8. Résultats (11 nov., tests simulés — distincts du réel)

- `python -I tests/webview2-runtime/run_tests.py --bootstrapper <MicrosoftEdgeWebview2Setup.exe téléchargé, jamais exécuté>` :
  `runtime_test` 153, `install_test` 91 (90 sans bootstrapper), `host_test` 7 scénarios (recover 16,
  not-detected 16, loader-missing 8, denied 12, close-during-install 12, relaunch 18,
  install-running 14), 0 échec.
- Confiance réelle : loader du SDK = Microsoft OK (refusé comme installateur : produit inattendu) ;
  copie altérée d'un octet = `0x80096010` ; non signé = `0x800B0100` ; `python.exe` (Python
  Software Foundation) = éditeur refusé ; vrai bootstrapper = accepté (racine applicative 2011).
- Réel, lecture seule, sur ce PC : la sonde du loader livré 0.3.132 rend `0x80070002`, décision
  *runtime absent, installation proposée* (`loader_probe`) — aucun environnement créé, aucun
  installateur lancé.
- Build estampillé (`tools/release/build.py`) OK ; fixtures existantes incluant `main.cpp`
  (shortcuts, navigation_errors, tab_drag, split_ui, accessibility, bookmarks_ui) OK.

## 9. Limites

- Les tests simulent l'installation : ils ne prouvent ni une installation réelle, ni le
  comportement d'Edge Update sur un runtime partiel sans enregistrement, ni la cause de la
  disparition intermittente. L'installateur n'a **pas** été exécuté sur ce PC (état préservé).
- Plantage du navigateur entre la création suspendue et la reprise : le processus suspendu n'a
  rien exécuté et ne détient pas de marqueur valide ; il reste suspendu (inoffensif) jusqu'à la
  fin de session. Plantage pendant que l'installateur tourne : le marqueur reste valide tant
  qu'il vit, puis devient périmé et est nettoyé.
- Mutex `Local\` : la coordination couvre les processus d'une même session Windows ; le marqueur
  (par utilisateur) reste vérifié sous ce mutex.
- Une stratégie d'entreprise qui bloque l'installation est visible par le code de sortie ; aucun
  contournement. Pas de « réparation » non documentée, de désinstallation ni d'écriture registre.
- Au plus 2 installations lancées par l'utilisateur par processus, aucune pendant qu'un
  installateur vit (même lancé par une autre instance), aucune après un installateur allé au bout
  sans runtime détecté. Chaque tentative exige un clic.
- Le lien Microsoft s'ouvre dans le navigateur par défaut ; si c'est ce navigateur, sa nouvelle
  instance affiche le même dialogue.
- Révocation vérifiée sur toute la chaîne : un proxy qui bloque CRL/OCSP fait refuser la signature.
