# Récupération WebView2 au démarrage — diagnostic, spec, plan

Statut : implémenté sur `VALXXII/webview2-startup-recovery` (base `origin/main` 599da56).

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
4. **Installation** (`src/webview2-install.h`) après clic seulement, une seule à la fois
   (boutons verrouillés pendant l'opération, double clic ignoré par la machine d'état) :
   - téléchargement WinHTTP (mêmes limites que le transport de l'updater : proxy système, délais
     10/10/15/20 s, TLS 1.2/1.3 sans relâchement, ≤ 6 redirections manuelles, pas de cookies ni
     d'authentification — mais un objet distinct : la politique GitHub de l'updater reste intacte) depuis
     l'URL exacte ci-dessus ; redirections acceptées seulement en HTTPS:443, sans identifiants,
     hôte `microsoft.com` ou `*.microsoft.com` ; taille ≤ 16 Mio ;
   - fichier écrit dans un dossier neuf au nom aléatoire (BCryptGenRandom) sous `%TEMP%`, DACL
     protégée (utilisateur courant + SYSTEM), `CREATE_NEW` ; rouvert en lecture avec partage
     lecture seule pendant la vérification **et** l'exécution (pas d'échange de fichier possible) ;
   - `WinVerifyTrust` sur ce handle (`WINTRUST_ACTION_GENERIC_VERIFY_V2`, révocation de toute la
     chaîne ; seul le retour `0` vaut confiance), puis la chaîne du signataire
     (`WTHelperGetProvSignerFromChain`) doit satisfaire `CERT_CHAIN_POLICY_MICROSOFT_ROOT` **et** le
     certificat signataire doit être `CN=Microsoft Corporation`, `O=Microsoft Corporation` **et** la
     ressource de version (signée) doit nommer `MicrosoftEdgeUpdateSetup.exe`. Le sujet seul ne vaut
     jamais attestation : il n'est lu qu'après la validation cryptographique et la racine Microsoft.
     **Mesure** : avec `dwFlags = 0`, la politique `MICROSOFT_ROOT` *refuse* la chaîne du vrai
     bootstrapper (racine « Microsoft Root Certificate Authority 2011 ») ; elle l'accepte avec
     `MICROSOFT_ROOT_CERT_CHAIN_POLICY_CHECK_APPLICATION_ROOT_FLAG`, que la doc décrit comme la
     vérification de « the Microsoft application root "Microsoft Root Certificate Authority 2011" ».
     Le code essaie donc les deux (racines Microsoft historiques, puis racine applicative 2011) —
     sans quoi l'installation officielle serait refusée ; aucune racine tierce n'est acceptée ;
   - `CreateProcessW(chemin, "\"chemin\" /silent /install")`, `CREATE_NO_WINDOW`, pas d'héritage,
     pas d'élévation ; attente annulable, bornée à 10 min ; code de sortie journalisé ;
   - nettoyage du fichier et du dossier ciblés (meilleur effort, journalisé).
5. **Après l'installateur** (quel que soit le code de sortie) : nouvelle sonde. Introuvable →
   « Installation terminée mais WebView2 reste introuvable » (le code de sortie est montré), plus
   de bouton Installer (pas de boucle), Réessayer + lien manuel. Trouvé → nouvelle
   `CreateEnvironment` ; **seul son succès** annonce la récupération et lance `RestoreSession`
   (une seule fois). Si la création échoue encore en « runtime absent » alors que la sonde le
   voit : bouton **Redémarrer le navigateur** — relance du même exe avec les mêmes arguments plus
   `--webview2-recovered`, puis fermeture de l'instance courante (qui n'a jamais restauré la
   session : pas de doublon, pas d'écrasement de `session`). Une instance lancée avec ce drapeau
   ne propose plus de relance (borne = 1).
6. **Courses** : chaque tentative de création porte une génération ; un rappel périmé ou arrivé
   après la fermeture est ignoré (pas d'écriture vers un HWND détruit). Fermeture pendant le
   téléchargement : annulation + abort WinHTTP ; pendant l'installateur : l'attente est abandonnée,
   l'installateur Microsoft continue seul (le tuer en cours d'installation serait pire) ; le
   prochain démarrage refait la sonde.
7. **Logs** : étapes, HRESULT, version, cause, source d'override (nom, pas de valeur complète),
   octets, code de sortie, sujet du signataire. Jamais d'URL de navigation, de cookie, de contenu
   de profil ni d'environnement complet.
8. **Tests** : hooks uniquement par injection (`Effects`, `Hooks`) dans les binaires de test ;
   aucune variable d'environnement ni drapeau de production n'affaiblit TLS/URL/signature.

## 5. Plan / fichiers

| Fichier | Rôle |
|---|---|
| `src/webview2-runtime.h` | Classification, lecture des overrides, accès loader (`Loader`), contrôleur de récupération (machine d'état pure, générations, bornes) |
| `src/webview2-install.h` | Politique d'URL, dossier privé, téléchargement, vérification Authenticode/éditeur/produit, lancement, worker annulable. Header-only ; winhttp/wintrust/crypt32/version résolus à l'exécution depuis System32 (`LOAD_LIBRARY_SEARCH_SYSTEM32`) : aucune nouvelle dépendance de link, compile dans les builds estampillés, preview et les fixtures non estampillées. `update-core` n'est **pas** modifié |
| `src/main.cpp` | `wWinMain` → `StartEngine()` ; hôte du contrôleur (loader, sonde, création, vue Modal, relance) ; `WM_APP+41` du worker ; `StopEngineStart()` au `WM_DESTROY` ; `Fail` reste pour les erreurs après démarrage |
| `tests/update-integration/run_all.py` | lance aussi `tests/webview2-runtime/run_tests.py` (aucun changement de `build.py`, du paquet ni du format de mise à jour) |
| `tests/webview2-runtime/` | `run_tests.py`, logique/contrôleur, installeur (faux réseau, faux setup lancé pour de vrai, vraie confiance sur fichiers signés locaux et sur le vrai bootstrapper non exécuté), faux `WebView2Loader.dll` + fixture hôte sur le vrai `main.cpp` (6 scénarios, captures), sonde lecture seule du vrai loader |

## 6. Points d'intégration pour le chantier fenêtres privées / processus séparés

- **Init** : `StartEngine()` (appelé dans `wWinMain` après la fenêtre, les surfaces et `StartUpdates()`)
  remplace le bloc `LoadLibraryExW … CreateCoreWebView2EnvironmentWithOptions`. Tout autre
  processus navigateur (fenêtre privée, processus séparé) doit appeler le même `StartEngine()` (ou
  un `WebView2Runtime::Controller` avec les mêmes hooks : même `EngineOptions`, même profil) et
  ne restaurer sa session que dans `EngineReady()` (hook `ready`, appelé une seule fois).
- **Cleanup** : `StopEngineStart()` au `WM_DESTROY` (annule téléchargement/vérification ; un
  installateur Microsoft déjà lancé n'est jamais tué), puis `engineStart.reset()` avant
  `FreeLibrary(loader)` en fin de `wWinMain`.
- **Handoff updater (#60)** : inchangé. `ForwardToInstalled()` reste avant toute fenêtre ; une
  fermeture pour installation de mise à jour (`closeWithoutPrompt`) passe par le même `WM_CLOSE`.
  La relance WebView2 (`RelaunchBrowser`) relance l'exécutable courant (déjà résolu par le
  forwarding) avec ses arguments + `--webview2-recovered` ; un processus privé devra transmettre
  son propre marqueur de mode, jamais celui d'une session normale.
- Le contrôleur ne touche jamais au profil ni à la session ; les transactions du store
  interprocessus n'ont rien à changer. Une instance en récupération n'écrit pas `session`
  (`sessionReady` reste faux).
- Si plusieurs processus partagent un dossier de données, une installation doit rester unique :
  à coordonner (mutex nommé par utilisateur) lors de l'arrivée des processus séparés ; aujourd'hui
  un seul processus navigateur par lancement.

## 7. Résultats (11 nov.)

- `python -I tests/webview2-runtime/run_tests.py --bootstrapper <MicrosoftEdgeWebview2Setup.exe téléchargé> --loader <loader livré 0.3.132>` :
  `runtime_test` 141 vérifications, `install_test` 50, `host_test` 6 scénarios (recover 15,
  not-detected 14, loader-missing 8, denied 12, close-during-install 10, relaunch 13), 0 échec.
- Confiance réelle : loader du SDK = Microsoft OK (refusé comme installateur : produit inattendu) ;
  copie altérée d'un octet = `0x80096010` (TRUST_E_BAD_DIGEST) ; programme non signé =
  `0x800B0100` (TRUST_E_NOSIGNATURE) ; `python.exe` (Python Software Foundation, signature
  valide) = éditeur refusé ; vrai bootstrapper = accepté (racine applicative 2011).
- Avant/après sur ce PC, en lecture seule : avant, `CreateEnvironment : 0x80070002` → boîte fatale ;
  après, la sonde du loader livré 0.3.132 rend `0x80070002`, décision *runtime absent,
  installation proposée* (`loader_probe`), dialogue natif au lieu de la boîte fatale
  (captures `design/webview2-startup-recovery/*.png`, rendues par la fixture hôte).
- Build estampillé complet (`tools/release/build.py`) et compilation non estampillée : OK, sans
  nouvelle dépendance de link.

## 8. Limites

- Les tests simulent l'installation : ils ne prouvent ni une installation réelle, ni la cause de
  la disparition intermittente. L'installateur n'a **pas** été exécuté sur ce PC (état à préserver).
- Si une stratégie d'entreprise bloque l'installation, le code de sortie est montré et le lien
  manuel proposé ; aucun contournement.
- Un registre incohérent que le bootstrapper refuse de corriger (sortie 0, runtime introuvable)
  est signalé tel quel ; pas de « réparation » non documentée, pas de désinstallation, pas
  d'écriture registre.
- Au plus 2 installations lancées par l'utilisateur par processus ; aucune après un installateur
  terminé sans runtime détecté, ni pendant qu'un installateur lancé tourne encore (délai de
  10 min dépassé). Chaque tentative exige un clic.
- Le lien « Page Microsoft WebView2 » s'ouvre dans le navigateur par défaut ; si c'est ce
  navigateur, sa nouvelle instance affichera le même dialogue (pas d'autre canal sans WebView2).
- La révocation est vérifiée sur toute la chaîne : si le proxy bloque l'accès aux CRL/OCSP de
  Microsoft, la signature est refusée (code affiché) plutôt que de relâcher la vérification.
- Le bootstrapper télécharge lui-même le runtime ; ses propres échecs (stratégie, réseau) ne sont
  connus que par son code de sortie, affiché tel quel.
