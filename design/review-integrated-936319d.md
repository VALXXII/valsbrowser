# Revue indépendante du SHA intégré 936319d : coffre (B03/B04/B05/E8), capture sans stockage web, F11, remplissage, hôtes de test, fusions PR64 / retrait « Plus d'infos »

Branche `VALXXII/Design-missing-elements`, HEAD `936319d`. Diff revu : `git diff 7a344e4 936319d -- src tests`.
Revue en lecture seule. Seul fichier écrit : ce rapport. Aucun test, aucun build, aucune E2E lancés : une E2E tournait sur
ce worktree et `build/` était hors limites. Les numéros de ligne renvoient à `936319d`.

## Verdict

**Pas encore activable.** Deux points bloquent :
- **N1** : l'armement de 500 ms du popover B03 n'agit sur aucun contrôle réel. Le choix d'un compte, c'est-à-dire le
  remplissage, contourne `Armed()`.
- **N13** : la capture n'est pas liée de façon établie au document qui l'a soumise. La règle des 2 s et l'exemption de
  toute redirection peuvent garder l'offre, avec son secret, après une navigation indépendante ou vers une autre origine,
  et afficher l'invite B04 au-dessus d'une page d'une autre origine.

Les deux corrections sont petites. Tout le reste est corrigé ou relève de limites acceptées (V7 partiel), avec des points
bas listés plus bas.

Le blocage #1 du coordinateur (secret en `sessionStorage`) est **levé** : il ne reste aucune copie en stockage web. Le
blocage #2 / F11 est **levé** : un état illisible n'est jamais pris pour un coffre vide. F1 à F8 et §6 sont confirmés dans
le code. Les macros de test ne peuvent pas entrer dans dev/release.

## Constats

| # | Gravité | Où | Bloque l'activation ? |
|---|---|---|---|
| N1 | **MEDIUM** | `src/modal.h:1001` (et `:1002`, `:1023`, `:1121`) ; `src/origin-prompt.h:285` | **Oui** (contrat §5 explicite, correction triviale) |
| N13 | **MEDIUM** | `src/elements-integration.h:997-1002` (`VaultNavigationStarting`), `:965`, `:297-314` (`PumpPrompt`) ; `src/main.cpp:2370` | **Oui** (le lien capture ↔ document n'est pas établi) |
| N2 | LOW | `src/origin-prompt.h:269,276` ; `src/modal.h:947` | Non (à corriger avec N1) |
| N3 | LOW | `src/elements-integration.h:962` (+ commentaire `:904-909`) | Non |
| N4 | LOW | `src/elements-integration.h:974` | Non (recommandé) |
| N5 | LOW (V7) | `src/origin-prompt.h:258-261,265` ; `src/modal.h:837,840,949,482` ; `src/elements-integration.h:946-955` ; `src/origin-prompt.h:398-400` | Non (V7 partiel accepté), mais l'affirmation de la table intégrateur est inexacte |
| N6 | LOW | `src/main.cpp:4000-4002` ; `src/elements-integration.h:1021-1029` | Non |
| N7 | LOW | `src/elements-integration.h:963,922-934,942` | Non |
| N8 | LOW | `src/password-demo.h:291` ; `src/elements-integration.h:1080-1094` | Non |
| N9 | LOW | `src/trace-erase.h:23-27` | Non (A5, hors coffre) |
| N10 | LOW | `src/password-vault.h:245` | Non |
| N11 | Info | `src/main.cpp:3555,3821` ; `src/chrome.h:788-790` | Non |
| N12 | LOW (test seul) | `src/e2e-fixture-pin.h:31-36` | Non |

### N1 (MEDIUM, bloquant) : le popover B03 n'est pas armé là où l'on choisit
`OriginPrompt::Build` pose `m.armMs = 500` pour `PasswordFill` réel (`origin-prompt.h:285`). Mais `Modal::Dialog::Armed()`
n'est consulté que dans `Press()` (`modal.h:944`). Or le popover B03 n'a **aucun bouton** : on choisit en activant une
ligne de compte, et ce chemin ferme directement la boîte avec `Collect(AcceptButton)` et `r.select` (`modal.h:1001`), sans
passer par `Press()`. Les autres chemins qui sautent l'armement :
- « Gérer les mots de passe » (`ManageHit`, `:1002`) ;
- Entrée sur la ligne focalisée (`:1023`) ;
- Espace (`:1027`) ;
- UIA Invoke (`WM_APP+1`, `:1121`).

Sur ce popover, `armMs` n'a donc **aucun effet**. Le test unitaire (`tests/modal_test.cpp:432-437`) ne couvre que les
boutons B04. L'E2E attend 650 à 700 ms avant chaque Invoke (`tests/e2e/vault_e2e.ps1:35,98`), donc elle ne peut pas le voir.

Scénario concret : le focus initial du popover est sur la **première ligne** (`InitialStop`, `modal.h:887`), et
`WM_KEYDOWN` n'ignore pas l'auto-répétition (`modal.h:1117`).
1. L'utilisateur atteint la clé 906 au clavier et presse Entrée. La clé est armée, le popover s'ouvre.
2. La touche est encore tenue au premier `WM_KEYDOWN` répété (délai de répétition Windows : 250 à 1000 ms).
3. Ce message atteint le popover et remplit **le premier compte sans choix délibéré**.

Une page de la même origine peut aussi inciter à « maintenir Entrée », ou à double-cliquer si le popover recouvre la
zone pointée. C'est précisément ce que le §5 demande d'empêcher : « the popover's actionable rows ignore activation until
armed… no account preselected ». L'origine reste correctement vérifiée, donc pas de fuite inter-origine. Le défaut est
un choix de compte, et un remplissage, que l'utilisateur n'a pas fait.

Correction minimale :
- dans `Activate`, refuser `hit` de 6200 à 6899 et `ManageHit` quand `!Armed()`, ou faire passer le choix de ligne par
  le même garde ;
- ignorer les `WM_KEYDOWN` répétés (bit 30 de `lParam`) pour Entrée et Espace dans le popover ;
- facultatif : focus initial sur la liste sans ligne active ;
- ajouter le test U « activer la ligne 0 juste après Show ne ferme pas la boîte ».

### N13 (MEDIUM, bloquant) : la capture n'est pas liée au document qui l'a soumise (point du coordinateur)
`VaultNavigationStarting` (`:997-1002`) applique deux règles :
- `if(!t||redirected)return;` : **toute** navigation redirigée est ignorée, quelle que soit son origine ;
- `if(GetTickCount64()-it->second<2000)return;` : **toute** navigation démarrée moins de 2 s après l'**arrivée** de la
  capture est réputée être la soumission.

Rien ne compare `navigationId`, l'URI cible ni l'origine. Le seul lien est de **temps**, et il ne prouve pas que la
navigation est la soumission. Le commentaire (« the tab's NEXT navigation drops it ») promet davantage que le code.

Le lien réellement établi : le secret est lié à l'**onglet** (`serial`, `weak_ptr`) et à l'**origine** de la capture.
Celle-ci vient de la Source moteur au moment du message. Il n'est **pas** lié au document : `navStamp` et
`navigationId` ne sont pas mémorisés. L'enregistrement se fait toujours sous l'origine capturée, donc aucun cas ne permet
d'écrire sous une autre origine ni de lire un secret. Le défaut porte sur la **durée de vie du secret** et sur une
**interface de confiance affichée hors contexte**.

| Cas | Comportement à 936319d | Attendu |
|---|---|---|
| Navigation indépendante démarrée moins de 2 s après la capture (lien cliqué, `location=` en JS, saisie dans la barre), vers **une autre origine** | **Gardée.** L'invite reste ouverte, ou s'affiche plus tard depuis la file, pendant que l'onglet montre b | Abandon |
| Même cas, même origine | Gardée | Acceptable une fois (cela peut être la soumission) |
| Redirection vers une autre origine (`IsRedirected`) | **Toujours gardée**, même à plus de 2 s, si la navigation initiale a été gardée ou a démarré avant l'arrivée du message | Abandon |
| Navigation échouée (`NavigationCompleted` avec `ok=false`, page d'erreur) | Aucun traitement : si elle a démarré en moins de 2 s, l'offre **reste** au-dessus de la page d'erreur, avec un mot de passe dont l'acceptation n'est pas prouvée | Abandon (fail-safe) |
| Capture plus récente | L'ancienne reste et la nouvelle est avalée (N3) | La nouvelle remplace l'ancienne (ou les deux sont abandonnées) |
| Ordre inversé : `NavigationStarting` de la soumission avant le message | La soumission n'est pas comptée. La **prochaine** navigation, si elle a lieu moins de 2 s après le message, est gardée à tort | Voir la correction plus bas |

**L'invite B04 peut-elle s'afficher au-dessus d'une page d'une autre origine ? Oui**, de deux façons :
1. Elle est déjà ouverte et la page navigue en moins de 2 s, ou par redirection, vers b. Rien ne la ferme :
   `ElementsNavigationStarting` ne ferme que Permission, SiteStatus et ClientCertificate.
2. Elle attend en file (autre invite ouverte, ou onglet en arrière-plan), puis `PumpPrompt` (`:297-314`) l'affiche plus
   tard sans revérifier l'origine actuelle de l'onglet.

Le titre dit « Pour https://a », mais le popover est ancré à l'indicateur d'origine, qui affiche b. Une page b hostile
peut s'arranger pour suivre immédiatement une capture de a : par exemple a (attaquant) poste une fausse capture puis
navigue vers une banque. L'invite native de confiance apparaît alors « sur » la banque. Elle ne peut enregistrer que sous
a, mais elle prête à confusion, et le secret vit plus longtemps que son document. Comme demandé, **abandonner ou refuser
l'offre est préférable** quand aucun signal sûr ne distingue les cas.

**Avis sur la correction candidate du coordinateur** :
- abandonner à **toute** navigation de premier niveau, redirections comprises, dont l'origine cible diffère de celle de
  la capture ;
- pour la même origine sans redirection, ne garder que la première navigation après la capture et abandonner à la
  deuxième ;
- aucune fenêtre de temps.

Elle va **dans le bon sens et suffit pour l'origine**. Elle supprime les deux cas les plus graves : l'autre origine et la
redirection vers une autre origine. Elle est **insuffisante seule** sur quatre points, que j'exige en plus :
1. **Ordre des événements.** Le message peut arriver après le `NavigationStarting` de la soumission. « La première
   navigation après la capture » serait alors une navigation indépendante, gardée à tort une fois.
   Exiger : mémoriser dans l'onglet le `navigationId` de la dernière navigation de premier niveau démarrée et non encore
   terminée (`NavigationStarting` / `NavigationCompleted`). Si une navigation est **en cours** à l'arrivée du message,
   c'est elle, la soumission, qui a consommé le droit de passage. On abandonne alors à la prochaine navigation non
   redirigée dont le `navigationId` diffère.
   Avec ce suivi, les redirections se reconnaissent aussi par leur **même `navigationId`**, plutôt que par
   `IsRedirected` seul.
2. **Navigation échouée.** Abandonner à `NavigationCompleted(ok=false)` de la navigation tolérée, et aussi quand une page
   d'erreur, TLS ou interstitielle est montrée (`t->errorPage.shown` / `t->https.shown`). Les gestionnaires HTTPS et
   d'erreur annulent puis naviguent vers `data:` : l'origine cible vide doit compter comme « autre origine », donc
   abandon.
3. **Garde à l'affichage et à la réponse.** Dans `PumpPrompt`, avant d'afficher une invite `PasswordSave` ou
   `PasswordUpdate` réelle, puis de nouveau dans le lambda de réponse avant `Save`, exiger
   `Vault::CanonicalOrigin(EngineSource(t))==origin` (Source moteur, pas `t->uri`), sans page d'erreur ni TLS montrée.
   Sinon, abandonner et effacer. Cette défense en profondeur rend impossible l'affichage au-dessus d'une autre origine,
   même si un chemin de navigation est raté (`SourceChanged` sans `NavigationStarting`, par exemple un `pushState`
   inter-documents impossible, ou un futur changement moteur).
4. **Capture plus récente (N3).** Une nouvelle capture du même onglet appelle `VaultCaptureDrop(t)` puis la remplace.

Effet de bord à accepter : une connexion dont le formulaire poste vers **une autre origine** (`login.exemple.fr` →
`auth.exemple.fr`, SSO) perdra l'offre. C'est fail-safe et conforme à la préférence « abandonner plutôt que garder ». À
consigner comme limite connue. Pas de stockage web ni de garde relâchée : la correction n'en introduit aucun.

Tests à exiger :
- **U** sur la logique pure (extraire une petite machine à états testable) : navigation indépendante vers une autre
  origine, redirection vers une autre origine, échec, deuxième navigation de même origine, ordre inversé
  message/navigation, capture plus récente ;
- **E2E** dans l'hôte de test : soumission, puis `location='https://autre…'` immédiat → aucune invite, journal
  « dropped », secret absent du profil.

### N2 (LOW) : « Mettre à jour » sans validation (effet de bord de F3)
Pour une mise à jour, le bouton n'est plus le bouton par défaut (`origin-prompt.h:276`). Or `Press()` ne valide que le
bouton par défaut (`modal.h:947`). Si l'utilisateur vide « Identifiant » puis clique « Mettre à jour », le validateur
(`:269`) ne s'exécute pas. `Map` renvoie alors `user=""` et `vault.Save(origin,"",pw)` **ajoute** une entrée sans
identifiant au lieu de mettre à jour le compte affiché. L'action vient de l'utilisateur et la page n'y gagne rien.

Correction : valider pour tout bouton `Accept` des invites identifiants, ou marquer le bouton « valide » sans Entrée par
défaut. F22 de la revue précédente subsiste aussi : `m.validate=nullptr` à `:265` est écrasé à `:269`. Il est sans effet
tant que N2 n'est pas corrigé, mais il le redeviendra.

### N3 (LOW) : une capture plus récente ne remplace pas l'ancienne
Le commentaire `:904-909` et la table intégrateur annoncent un effacement « à une capture plus récente ». Le code fait
l'inverse : si une invite B04 est ouverte ou en file pour l'onglet (`vaultPromptOpen`, `:962`), la nouvelle capture est
avalée et effacée, et **l'ancienne reste**.

Cas concret :
1. L'utilisateur se trompe de mot de passe.
2. La page répond par un rejet SPA, sans navigation.
3. Il ressaisit et soumet de nouveau.
4. L'invite propose toujours le premier mot de passe, le mauvais.

Si la seconde soumission navigue, l'ordre des événements décide. La seconde capture peut être avalée, puis la navigation
à plus de 2 s supprime la première, et il n'y a plus d'invite du tout. Le défaut ne touche ni la confidentialité ni
l'origine, mais il fait enregistrer un mot de passe faux.

Correction : sur une nouvelle capture de la même origine pour le même onglet, appeler `VaultCaptureDrop(t)` puis traiter
la nouvelle.

### N4 (LOW) : l'origine des connexions part dans le journal
`Log(L"VAULT capture: B04 prompt for "+origin)` (`:974`) inscrit dans `prototype.log` chaque origine où un mot de passe
a été soumis. C'est une trace d'usage propre au coffre, à contre-courant de F21 et de l'esprit D3. La revue précédente
notait « Log lines carry no user, origin or secret ». La fenêtre privée reste muette (`Log` silencieux). Le journal
contient déjà toutes les URL (`NAVIGATION OK`), donc l'ajout est incrémental.

Recommandé : retirer l'origine de cette ligne.

### N5 (LOW, V7) : des copies hôte du mot de passe soumis restent non effacées
Les correctifs F6 sont réels :
- `TakeString` ;
- appelant de `PageMessage` ;
- `PromptJob` (déplacement, affectation, destructeur ; efface la capacité, donc aussi le tampon SSO) ;
- `promptActive.spec` après `Show` ;
- `Prompt::spec_.secret` ;
- `Result.values` ;
- `Field.value` à la fermeture ;
- tampons EDIT.

Restent des copies **dans le code hôte, donc maîtrisables**, qui ne sont pas des internes STL ou WebView2 :
- `Build` : `Field pass` local (`origin-prompt.h:259-260`) et les éléments de `initializer_list` de `m.fields = {user, pass}`
  (`:261`) ou `{pass}` (`:265`). Ce sont 2 à 3 copies sur le tas, libérées sans effacement, pour tout mot de passe de 8
  caractères ou plus.
- `Values()` (`modal.h:837`) : les vecteurs temporaires de `validate(Values())` (`:949`, Enregistrer) et de `Revalidate`
  (`:840`, à chaque frappe après une tentative). `Invalid()` (`:482`) appelle aussi `Text()` sur les champs requis
  (l'identifiant seulement).
- `JsonField` : la croissance de `out+=` sans `reserve` (`elements-integration.h:946-955`), point V7 #3 inchangé.
- `Prompt::Show(Spec spec, …)` par valeur (`origin-prompt.h:398-400`) : un SSO résiduel de 7 caractères au plus dans le
  paramètre déplacé. C'est le cas STL accepté.
- Concaténations de `FillScript` (V7 #6, inchangé).

La table intégrateur dit « valeurs … des champs de la modale effacées » : c'est inexact pour `Build` et `Values()`. Ce
n'est pas bloquant si V7 partiel reste accepté. Il faut cependant le documenter, ou poser `reserve` et effacer ces
temporaires.

### N6 (LOW) : l'armement de la clé 906 est incomplet par rapport au §5
- Le clic se décide au relâchement (`main.cpp:4000-4002`, `original==id` puis `Command`). Un appui commencé **avant**
  l'armement et relâché après active la clé. Le §5 demande que l'appui et le relâchement tombent tous deux après
  l'armement.
- Le minuteur ne repart pas sur la réactivation de la fenêtre, ni sur un déplacement ou un redimensionnement. Seule la
  condition « premier plan » est vérifiée au clic, et un clic depuis une autre application active la fenêtre puis la clé
  dans le même geste.
- Une page peut garder la clé **désarmée** indéfiniment par des `pushState` répétés (`SourceChanged` incrémente
  `navStamp`, puis `SyncVaultKey` réarme). C'est un déni de service du remplissage, sans fuite.

C'est faible, parce que la page ne contrôle ni la position de la clé (chrome natif) ni son apparition (elle dépend du
coffre et de l'origine).

### N7 (LOW) : F9 et F10 partiels
- Une page peut toujours provoquer, à chaque message `{browserVault:'capture'}`, une prise de verrou d'au plus 1 s sur
  le thread UI, une relecture du fichier, puis un toast « Mot de passe non enregistré » quand le coffre est illisible
  (`:963`). `LoadVault` passe aussi **avant** le contrôle du contenu du message (`:964`). Une seule invite par onglet
  limite le cas où le coffre est sain, mais pas le cas illisible : boucle de toasts possible. Suggestion : un état « échec
  mémorisé » tant que la date de modification du fichier ne change pas, et aucune relecture déclenchée par la page.
- `VaultCached` → `ReadVault(0)` journalise l'échec toutes les 2 s tant que le coffre est illisible (`:927-929,942`).
  `BuildChrome` tourne souvent, donc le journal se remplit.

### N8 (LOW) : F15 résiduel
`SetReal` remplace toujours `accounts_` sans refuser quand la surface est visible (`password-demo.h:291`).
`ShowVaultManager` appelle `SetReal` avant `Show` (`:1085-1094`). Depuis F1, la suppression porte l'id de
`accounts_[current_]`, donc plus d'erreur d'index sur le **même** tableau. Mais si le gestionnaire est rouvert pendant
qu'il est visible, les lignes affichées et `accounts_` peuvent diverger. La « Confirmation » supprime alors le compte
`accounts_[current_]` de la nouvelle liste, pas celui qui est affiché. Je n'ai pas trouvé de chemin d'UI évident vers
cette réouverture, mais le garde manque. Correction : ignorer `ShowVaultManager` si `passwordDemo.Visible()`.

### N9 (LOW) : F7, effacement des sauvegardes sans retour d'échec
`DropDamagedBackups` ignore l'erreur de `remove` (`trace-erase.h:23-27`). Si un antivirus ou un indexeur tient le
fichier, la copie endommagée survit alors que l'effacement est annoncé « Done ». Un échec devrait remonter en `Failed`.

### N10 (LOW) : V2-L4 / F13 seulement en partie fermés
La garde `acceptable` teste `Digest(Header)` sur une constante (`password-vault.h:245`), pas le digest réellement écrit.
Si le hachage échoue pour le seul corps (cas transitoire, très improbable), `Write` émet encore `end\tN\t` et `Commit`
renomme. Le coffre devient « endommagé » et les écritures sont bloquées, mais les données sont conservées.

### N11 (Info)
- Le commentaire de `case 'Q'` est collé derrière `case VaultKeyId` (`main.cpp:3555`), et le commentaire « B01 chip »
  précède désormais le nœud UIA du coffre (`:3820-3821`).
- La clé se place à gauche de « Copier le lien », pas « à droite de l'indicateur d'origine » comme proposé au §5
  (`chrome.h:788-790`). L'écart est acceptable si Paper le valide.
- La visibilité ne demande pas « dernier `NavigationCompleted` réussi » ; les pages d'erreur et TLS sont toutefois exclues.

### N12 (LOW, test seul) : P-L3 encore permissif
Le manifeste de pin accepte encore des clés en double (la dernière gagne) et ignore les clés inconnues
(`e2e-fixture-pin.h:31-36`). Le port et `sha256` sont désormais stricts. Cela reste fail-closed par le pin exact.

## Vérifications demandées

### 1. F1 à F8, §5 et §6 (fe270aa, 8237c2a, c86bbcf, dde6765)
- **F1** : confirmé. `Account::id` est porté par chaque ligne et `remove_(accounts_[current_].id)`. Voir N8 pour le
  résidu F15.
- **F2** : confirmé. `ToUtf8` remplace un demi-substitut isolé par U+FFFD (`downloads.h:184`).
- **F3** : confirmé pour B04. `armMs=500` (`origin-prompt.h:275`), Update sans Entrée par défaut, `Press` gardé. Effet de
  bord : N2.
- **F4** : confirmé (`:960`).
- **F5** : confirmé.
  - `VaultNotice` couvre l'illisible, l'échec de commit avec `vault=vaultBase`, et les refus de remplissage.
  - Réglages affiche « Illisible » via `SIZE_MAX` (`settings-panel.h:215`).
- **F7 / F8** : confirmés. Résidu N9.
- **F12** : confirmé (`try/catch` → dommage).
- **§5** :
  - la clé 906 respecte les conditions de visibilité (`:1015-1020`) ;
  - armement 500 ms : onglet, `navStamp`, premier plan ;
  - source moteur relue au clic (`EngineSource`, refus si différente de `t->uri`) ;
  - au choix : onglet actif, premier plan, aucune interstitielle ni page d'erreur, `CanFill`, relecture fraîche, id et
    origine de l'entrée revérifiés ;
  - les refus sont affichés.
  - **Défaut : N1** (lignes du popover non armées). Résidus : N6.
- **§6** :
  - `Vault::WritesForbidden()=privateWindow` est posé en tout premier dans `wWinMain` et testé dans **le seul** chemin
    d'écriture (`Vault::Commit`). `VaultFile()` n'apparaît qu'aux deux appels de `Commit`.
  - Aucun script de capture en privé (`:1113`) et `VaultCapture` sort tôt (`:960`).
  - Gestionnaire en lecture seule, sans action inerte (« Retour » seul plus une note).
  - Relectures fraîches, même `dataDir`, verrou par mutex nommé (aucun fichier créé).
  - Autosave et autofill moteur à FALSE en privé (P1).
  - **Conforme.**

### 2. Blocage #1 : capture sans stockage web (d906ed9)
- `VaultCaptureScript` (`:910-915`) se limite à un `postMessage` unique. Il n'y a **plus aucun** `sessionStorage` ni
  `localStorage` dans `src/` (grep vide). L'E2E le vérifie en plus (`WebCopy`, scan de profil avec témoin positif).
- Le secret vit dans `OP::Spec::secret` : d'abord la tâche en file, puis `Prompt::spec_` et les champs de la modale.
  L'origine vient de la **Source moteur**, et le secret est lié à l'onglet par `serial` et `weak_ptr`.
- Points d'effacement vérifiés :
  - **réponse** : le lambda efface `r.password`, `FinishPrompt` aussi, `Prompt::Show` efface `spec_.secret` et
    `values` ;
  - **fermeture d'onglet** : `ElementsTabClosed` → `AbortTabPrompts` (file et invite ouverte) ;
  - **changement d'onglet** : `PromptTabChanged` → `Abort`. La capture est perdue, ce qui va dans le sens sûr ;
  - **navigation suivante** : `VaultNavigationStarting` → `VaultCaptureDrop` ;
  - **fermeture de fenêtre** : `Dispose` puis `WM_DESTROY` → `done(Result{})`, et destructeurs `PromptJob` ;
  - **capture plus récente** : **non** (N3).
- **L'heuristique des 2 s ne permet ni écriture inter-origine ni lecture, mais elle n'établit pas le lien au document :
  voir N13 (bloquant).** Elle ne décide que du moment où l'on **jette** une capture. L'origine d'enregistrement est figée
  au message, depuis le moteur, si bien qu'aucune exception de 2 s ne permet d'enregistrer sous une autre origine ni de
  lire quoi que ce soit.
  - Les redirections serveur (`IsRedirected`) sont exemptées, ce qui est correct : une redirection appartient à une
    navigation déjà commencée.
  - Une page malveillante qui navigue dans les 2 s vers un autre site garde une invite « Pour https://a » pendant que
    la barre montre b. Le libellé reste honnête et l'enregistrement va sous a, avec ses propres données.
  - Limites d'usage, toutes fail-safe (perte de l'invite) : soumission différée en JS de plus de 2 s
    (`preventDefault` puis `form.submit()`), chaînes SSO à plusieurs navigations non redirigées, et ordre d'arrivée du
    message par rapport à `NavigationStarting` non garanti.
  - Les SPA (`fetch` puis `pushState`) ne déclenchent pas `NavigationStarting`. La capture reste donc jusqu'à la réponse
    ou la fermeture, toujours en mémoire hôte et liée à l'onglet.
  - **Pas d'exploitation en écriture ni en lecture.** En revanche, la durée de vie du secret et l'invite affichée
    au-dessus d'une autre origine restent ouvertes : N13.
- Les copies restantes sont listées en N5. `Result.password` et `JsonField out` sont effacés après usage ; la croissance
  de `JsonField` ne l'est pas.

### 3. Blocage #2 / F11 : états de lecture (d906ed9)
`ReadVault` (`:922-935`) lit sous le verrou les attributs du fichier puis son contenu. Résultat par cas :

| Cas | Résultat |
|---|---|
| Fichier absent | `ReadAll` renvoie vrai et vide, `exists=false` : coffre vide, correct |
| Fichier de 0 octet | `Unavailable` |
| Erreur de lecture, de partage ou d'accès (y compris un dossier nommé `passwords`) | `Unavailable` |
| Format inconnu ou fichier endommagé | `Unavailable` |
| Verrou occupé | `Busy`, le cache reste tel quel |

`Unavailable` remet `vault` à zéro et `vaultLoaded=false`. Un état illisible n'est **jamais** pris pour vide, donc il n'y
a ni invite « Enregistrer » à la place de « Mettre à jour », ni `Commit` sur une base vide.

- **Actions explicites** : `LoadVault(1000)` (capture, réponse B04, clic, choix, gestionnaire) relit toujours et affiche
  le refus.
- **Contrôles périodiques** : `VaultCached` tente le verrou à 0 ms au plus une fois toutes les 2 s. Il ne bloque pas.
- **Compte supprimé dans une autre fenêtre** : au choix, relecture puis `Find(id)` et contrôle de l'origine, avec le
  message « n'est plus enregistré ». Cela vaut en fenêtre normale comme en privée, puisque la lecture est permise en
  privé et que `dataDir` est commun.
- **Conforme.** Résidus : N7.

### 4. Retour de remplissage (c3c3f79)
- `FillScript` ne renvoie que des littéraux : `'origin'`, `'noform'`, `'nofield'`, `'filled'`. `ParseFillResult`
  n'accepte que ces 4 chaînes JSON exactes. Tout le reste, y compris une exception de page (`"null"`) ou un HRESULT en
  échec, donne `Failed`.
- Le JSON n'est ni journalisé ni affiché ; seul l'énumérateur l'est. Aucune valeur de page ni le secret ne reviennent.
- Une page de la même origine peut mentir (accroche du setter et réponse « filled » sans effet), mais elle a déjà le
  secret par construction. Ce cas est accepté.
- Contrôles inchangés : `location.origin`, `self.origin`, `isSecureContext`, `ancestorOrigins`, formulaire du champ
  focalisé, champs visibles. **Aucun relâchement.**
- « rempli » n'est journalisé que sur `Filled`. Échec d'`ExecuteScript` et sorties autres que `filled` donnent un
  `VaultNotice` honnête.

### 5. Code réservé aux hôtes de test
- `BROWSER_E2E_VAULT_HOST` n'est défini que par `tests/e2e/vault_host.cpp:5`. `BROWSER_E2E_FIXTURE_HOOK` n'est défini que
  par `tests/e2e/mtls_host.cpp:4`, `vault_host.cpp:4` et `e2e_pin_test.cpp:4`.
- `tools/release/build.py:126-131` ne passe que `-DBROWSER_HAS_BUILD_INFO`, éventuellement `-DBROWSER_PREVIEW`, et
  `-DTVG_STATIC`. `build.ps1` et les autres `test-*.ps1` ne définissent aucune de ces deux macros.
- Ce que le test lève, et seulement là :
  - la condition « premier plan » (`modal.h:902-904`, `elements-integration.h:1027-1029,1053-1055`) ;
  - `ActiveFlag()=true` (`main.cpp:4256-4258`).
- **L'armement de 500 ms est conservé** dans `Armed()` (le test du temps précède le `#ifdef`) et dans `VaultKeyArmed`.
- `tests/e2e/product_markers.ps1` contrôle l'absence des marqueurs UTF-16 « E2E VAULT HOST » et « E2E FIXTURE PIN » dans
  le produit, ainsi que « VAULT state: inactive » à l'exécution.
- **Conforme.** Le produit reste inactif : `VaultKeyVisible`, `VaultInfo` et `VaultCapture` sortent tôt si
  `!Vault::Active()`, et aucune lecture du coffre n'a lieu.

### 6. Fusions PR64 (f97e04a) et retrait de « Plus d'infos » (8eee59c)
- `git diff 7a344e4 936319d -- src/modal.h` ne contient que `armMs`/`Armed`, l'effacement des valeurs de champ et le glyphe
  `storage`.
- Aucun reste de « Plus d'infos » : grep de `MoreHit`, `WM_APP+4` et « Plus d'infos » vide dans `src/`. Les seuls
  `WM_APP+40/41` sont étrangers.
- B03/B04/B05 hors armement : rien n'a changé en focus, ordre clavier, hauteur ou défilement, ni nœuds UIA de la modale.
  Les invites de permission, de leur côté, ont reçu de nouveaux sous-titres et perdu la note des notifications.
- Échap, la croix et la désactivation d'un popover ancré mènent tous à `Dismiss`, et aucun reporté (deferral) n'est
  résolu par un contrôle qui n'est pas une réponse.
- **Conforme**, à part N1, qui est préexistant au modèle « ligne qui ferme » et que l'arming n'a pas couvert.

### 7. Stockages, sessions, A5, pin
- `profile-store.h` et `window-sessions.h` : non modifiés depuis 7a344e4.
- `trace-erase.h` : F7 et F8 corrects, résidu N9.
- `e2e-fixture-pin.h` : port et `sha256` stricts, résidu N12.
- Hôte mTLS : le filtre `ICoreWebView2_22` (requêtes des workers) est obligatoire, sinon `guarded=false` (`main.cpp:2409-2410`).

## Ce qui reste avant activation
1. **N1** : armer les lignes et le lien « Gérer » du popover B03, ignorer la répétition clavier, et ajouter le test U
   correspondant. **Bloquant.**
2. **N13** : appliquer la correction candidate du coordinateur (autre origine ou redirection inter-origine → abandon ;
   une seule navigation de même origine ; aucune fenêtre de temps), **plus** :
   - le suivi de `navigationId` pour l'ordre inversé ;
   - l'abandon en cas d'échec ou de page d'erreur ;
   - la garde de Source moteur à l'affichage et à la réponse ;
   - le remplacement par une capture plus récente (N3) ;
   - les tests U et E2E décrits.
   **Bloquant.**
3. Recommandés, tous petits :
   - N2 : valider « Mettre à jour » ;
   - N4 : l'origine sort du journal ;
   - N8 : refuser `SetReal` pendant que la surface est visible.
3. Limites acceptées, à garder visibles :
   - **V7 partiel** : copies hôte listées en N5, internes STL et WebView2, IPC du `ExecuteScript` ;
   - **B07 non concluant** (matrice) ;
   - **clic immédiat sur la clé non prouvé en E2E**, l'armement n'étant prouvé qu'en U, comme le dit l'E2E elle-même ;
   - P-I1 : le marqueur est vérifié sur `build/v2`, pas sur un artefact `release` reconstruit.

Une fois N1 et N13 corrigés et vérifiés, le coffre peut être déclaré **activable** sans nouvelle revue complète, sur
simple contrôle du diff. Pour N1 : test U, et idéalement un Invoke de ligne en moins de 500 ms dans l'hôte de test, qui
doit être ignoré. Pour N13 : tests U de la machine à états et E2E « navigation immédiate vers une autre origine ».

---

## Recontrôle 937a2d8 (diff `936319d..937a2d8 -- src tests`, y compris c67112d)

Lecture seule, sans build, test ni E2E (un passage complet tournait sur ce worktree). Les numéros de ligne renvoient à
`937a2d8`.

### État par constat

| # | État | Vérification dans le code |
|---|---|---|
| **N1** | **Corrigé** | `modal.h` : les lignes de compte qui répondent (mode sans sélection) et `ManageHit` retournent si `!Armed()`. Cela couvre le clic, Entrée, Espace et UIA `WM_APP+1`, qui passent tous par `Activate`. `WM_KEYDOWN` du dialogue ignore la répétition (bit 30) pour Entrée et Espace. `foregroundForTest_` n'est posé que par `ArmNowForTest(true)` et remis à faux dans `Show` : pas d'effet en produit. Test U : ligne et « Gérer » juste après `Show` sans effet, Entrée répétée sans effet, choix armé qui répond. Résidu R4 |
| **N13** | **Corrigé** | `Vault::CaptureBinding` (`password-vault.h`), testé en U sur les 8 cas : soumission et redirection de même origine, deuxième navigation, autre origine immédiate, redirection vers une autre origine, échec, ordre inversé (id en cours), origine vide, absence de liaison. Plus de fenêtre de temps. `Tab::navInFlight` est posé **après** l'appel au coffre dans `NavigationStarting` et remis à 0 à `NavigationCompleted` pour le même id. `SandboxNavigationStarting` ne court-circuite le coffre que lorsqu'il **annule**, sinon le coffre voit la navigation. `VaultNavigationCompleted` abandonne l'offre sur l'échec de la soumission et dès qu'une page d'erreur ou TLS est montrée. Défense en profondeur : `VaultOfferStillValid` (origine de la Source **moteur** égale à l'origine capturée, sans page d'erreur ni TLS) est vérifiée à la capture, dans `PumpPrompt` avant d'afficher un B04 réel (abandon et réponses vides) et dans le lambda avant `Save` (abandon et `Wipe`). **Le B04 ne peut plus s'afficher ni être accepté au-dessus d'une autre origine.** Un `navInFlight` périmé (navigation jamais terminée) ne peut que faire perdre l'offre (fail-safe). Une SPA (aucune navigation en cours) accepte au plus une navigation suivante de **même origine**, ce qui est acceptable. Effet de bord SSO (formulaire posté vers une autre origine) : documenté et voulu |
| N2 | Corrigé | `Press` valide aussi `AcceptButton`. Le validateur est posé avant la branche D1, qui le remet bien à `nullptr` (F22 réglé). Test U « identifiant vidé refusé » |
| N3 | Corrigé | Une capture plus récente appelle `VaultCaptureDrop` (file et invite ouverte, réponses vides, effacement), puis démarre une nouvelle liaison. Nouveau résidu R1 |
| N4 | Corrigé | Ligne « VAULT capture: B04 prompt » sans origine |
| N5 | Partiel accepté | `JsonField` : `reserve(json.size())`. Les copies de `Build` et `Values()` restent dans le V7 partiel, à documenter |
| N6 | Corrigé, sauf `pushState` | Appui souris armé exigé (`vaultKeyPress` capturé à `WM_LBUTTONDOWN`) et minuteur relancé à `WM_ACTIVATE`. Le déni de service par `pushState` reste un résidu faible documenté. Résidu R3 |
| N7 | Corrigé | Le contenu du message est analysé avant toute lecture. Un coffre illisible n'est plus relu ni réannoncé tant que l'empreinte du fichier (présence, date d'écriture, taille) ne change pas. `ReadVault` ne journalise chaque raison qu'une fois. Résidu R2 |
| N8 | Corrigé | `ShowVaultManager` est ignoré tant que le gestionnaire est visible |
| N9, N10, N11, N12 | Ouverts (LOW ou Info) | Inchangés. Non bloquants |

### c67112d : réglage changé volontairement en fenêtre privée
`SaveSettings` n'exclut plus `privateWindow`. Ses appelants :
- `SettingChanged`, appelé depuis le tiroir Réglages, `PickDownloadFolder` (sélecteur ouvert par l'utilisateur) et
  `ToggleBookmarkBar` (commande utilisateur) ;
- « Ne plus demander » à la fermeture d'une fenêtre à plusieurs onglets.

Les seules affectations hors de ces chemins (`settings.zoomPercent`, `main.cpp:2717,2719`) appartiennent au scénario
`--split-test`, où `smoke` vaut vrai et où `SaveSettings` retourne immédiatement. Les champs de `BrowserSettings::State`
ne contiennent aucune donnée dérivée de la navigation, seulement des valeurs choisies ou saisies par l'utilisateur
(page d'accueil, recherche personnalisée, dossier). Le fichier est écrit par la transaction normale, champ par champ.

Ce n'est **pas une fuite de navigation**, mais c'est un **changement de contrat** : le §6.8 de la revue 7a344e4 et la
matrice disaient « réglages en privé transitoires ». Le changement est conforme s'il s'agit d'une décision du produit,
ce que la matrice indique. Aucun texte d'interface en fenêtre privée ne promettait le contraire.

### Nouveaux résidus (tous LOW, non bloquants)
- **R1** : sans le garde « une invite par onglet », une page peut envoyer des captures en rafale. Chacune fait un
  `ReadVault(1000)` (verrou plus lecture sur le thread UI, attente d'au plus 1 s si une autre fenêtre tient le verrou) et
  remplace l'invite, ce qui la réarme. Résultat : l'invite clignote et ne peut jamais être armée, mais **seulement pour la
  propre origine de la page**. Suggestion : limiter le débit, au plus une capture par onglet et par seconde.
- **R2** : un verrou **occupé** au moment de la capture est traité comme « illisible ». `vaultCaptureFailed` est posé
  avec l'empreinte courante. Comme le fichier ne change pas, les captures légitimes suivantes sont **avalées sans
  avis**, et l'avis affiché parle d'« illisible » au lieu de « profil occupé ». Correction : ne mémoriser l'échec que pour
  `Unavailable`, pas pour `Busy`, et passer `r` réel à `VaultUnavailableText`.
- **R3** : `vaultKeyPress` peut rester à 0 après un appui non armé relâché hors de la clé. La prochaine activation de la
  clé au clavier est alors ignorée une fois. Gêne mineure, dans le sens sûr.
- **R4** : le filtre de répétition n'est que dans la procédure du dialogue. Le sous-classement des champs (`EditProc`,
  `modal.h:1065`) transmet encore Entrée répétée à `Key`, donc à `Press`. Pour un B04 « Enregistrer », dont Entrée est le
  bouton par défaut, une touche Entrée **maintenue plus de 500 ms** depuis la soumission du formulaire peut enregistrer
  sans décision. Seul le mot de passe que l'utilisateur vient de saisir, pour cette origine, est concerné. « Mettre à
  jour » n'est pas touché, faute d'Entrée par défaut. Correction : même test du bit 30 dans `EditProc`.

### Verdict 937a2d8
**Activable**, sous deux réserves factuelles que je n'ai pas pu vérifier moi-même : les tests U (`password_vault`,
`modal`, `elements_integration`) et l'E2E `vault_e2e` du passage en cours doivent être verts **sur 937a2d8**, et
`product_markers` doit confirmer « VAULT state: inactive » sur le produit jusqu'à la bascule de `ActiveFlag`.

Les deux points bloquants N1 et N13 sont corrigés dans le code. Ce qui reste n'est pas bloquant :
- R1 à R4 (corrections courtes, R2 et R4 recommandées avant ou peu après l'activation) ;
- N9 à N12 ;
- V7 partiel (internes STL et WebView2, plus les copies `Build` et `Values()`) ;
- B07 non concluant ;
- le clic immédiat sur la clé, prouvé en U seulement ;
- P-I1 vérifié sur `build/v2`.
