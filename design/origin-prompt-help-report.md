# Rapport — aide « Plus d'infos » des demandes d'autorisation

Branche `VALXXII/notification-permission`, basée sur `8237c2a` (PR63, committé). Contrat :
`design/origin-prompt-help.md`. Destination : adoption par l'intégrateur dans `VALXXII/Design-missing-elements`
(PR vers cette branche, aucun merge vers main, aucune publication).

## 1. Cause vérifiée de la capture x.com

* Journal 0.3.137 lu uniquement sur les lignes `PERMISSION request kind=` / `PERMISSION kind=` :
  `PERMISSION request kind=13 https://x.com`, puis `PERMISSION kind=13 action=0` (fermé, rien enregistré).
  Aucune autre donnée du profil personnel lue ; aucun test sur x.com.
* SDK local 1.0.4314.35 (`WebView2.h`) : `13 = COREWEBVIEW2_PERMISSION_KIND_PERSISTENT_STORAGE`.
  Microsoft : « permission to persist storage created by the Storage APIs, service workers, etc. »
* `OriginPrompt` ne connaissait que 0 à 12 : titre générique, icône info, pas de sous-titre.
  **Ce n'était pas une notification** : le chemin Notifications existant n'était pas en cause.

## 2. Sources Microsoft retenues (pour la copie)

| Fait | Source primaire | Effet |
|---|---|---|
| `SavesInProfile` FALSE = « not persist the state beyond the current request » | `ICoreWebView2PermissionRequestedEventArgs3` | « ne vaut que pour cette demande » |
| Notifications non gérées par l'hôte : interface par défaut de WebView2 ; `NotificationReceived` = notifications « non-persistent » | `ICoreWebView2_24`, `…NotificationReceivedEventArgs` | « même onglet fermé » retiré (non prouvé) |
| InPrivate : durée de conservation non documentée | `ICoreWebView2ControllerOptions` | aucune promesse de durée |

## 3. Paper (fichier Browser Design)

* Canonique `p-1-0` : planche dédiée « B02 · Permissions — Plus d'infos (portée, mémorisation) »
  (Notifications repos / survol / ouvert ; Stockage persistant ouvert / focus clavier ; Type non précisé).
  Planche B02 existante : rangée « Plus d'infos » au repos sur les 6 popovers, note Notifications retirée,
  sous-titre Notifications corrigé. Copie de lecture sur `p-F-0` (sous « 11 Dialogues »).
* Valeurs reprises des rangées utilisateur A10 (`QLE/QKP/QK3`, icône `QK5`) : h 32, rayon 6, écart 8 ;
  repos 0/4, survol inline 8 + fond blanc 10 %, ouvert 8/4 + icône à 45° ; paragraphe 14/20 blanc 70 %,
  inset 8.
* Incident corrigé : le « + » était noir parce que `write_html` avait enregistré
  `fill="color-mix(…var(…))"` comme `#000000`. Le remplissage a été réappliqué ; aucun nœud du lot n'a de
  remplissage noir.
* Protections : Icon vérifié identique avant et après (30 nœuds + racine, 14 enfants, code 0) ; jetons
  `b5366ac2` inchangés ; 8 fils de commentaires inchangés (`c-wvnu0v`, `c-npdt3g` ouverts) ; originaux
  `QL8/QKI/QJW/QA2/QGT`, A10 `PUX/S0Y` non touchés ; pas de chevauchement ; `finish_working_on_nodes` appelé.
* Captures : `build/paper-permission-help/{before,after,after2}/` (ignoré par git, dans le worktree).

## 4. Code (patch additif, sans reformatage)

* `src/origin-prompt.h` : `Permission::PersistentStorage = 13` (titre, sous-titre, glyphe base de données,
  nom) ; sous-titre court pour chaque type ; type 0 : « Le moteur ne précise pas de quoi il s'agit. » ;
  type supérieur à 13 : « Type non reconnu par ce navigateur (code moteur n). » ; `Text::More()` ;
  `Spec::requester`. La note Notifications passe dans le panneau (demande utilisateur).
* `src/modal.h` : `Spec::more` et la rangée « Plus d'infos » (mise en page, rendu, clic, Entrée/Espace,
  Tab, UIA ExpandCollapse et nœud Texte, défilement du paragraphe quand la place manque). La bascule
  n'appelle jamais `done`.
* `src/site-request.h` : glyphe stockage pour la puce. `src/elements-integration.h` : une ligne,
  `spec.requester = OriginOf(uri)`.
* Inchangé : politique de mémorisation (case ; notifications toujours mémorisées), deferral une seule fois,
  navigation, changement d'onglet, fermeture, groupement caméra + micro, fenêtre privée, coffre, TLS, A10.

## 5. Preuves

| Niveau | Fichier | Résultat |
|---|---|---|
| Unitaire, fenêtre réelle (U) | `test-popups.ps1 -SkipPixels` → `modal_test` | 290 vérifications, 0 échec (×2) ; 57 nouvelles (233 avant) |
| Transport simulé (faux arguments WebView2) | `test-elements-integration.ps1` | PASS (types 13, 4, 0, 14 ; deferral jamais complétée par le panneau, une fois à la navigation) |
| Moteur réel isolé (E2E) | `tests/e2e/permission_help_e2e.ps1` | 17/17 : runtime 157.0.4341.46, copie jetable, profil neuf, serveur local temporaire |
| Build estampillé | `tools/release/build.py` | 0.3.189, commit `11202b0` |

L'E2E réel montre que `navigator.storage.persist()` lève bien `PermissionRequested` avec `kind=13` et
`Notification.requestPermission()` avec `kind=4`. Le panneau s'ouvre et se ferme par UIA sans réponse
(promesse de la page toujours en attente). Navigation : `action=0` une seule fois. Bouton Fermer :
`action=0`, la page reçoit `denied`. Aucun clic « Autoriser ».

Rendus natifs à 96, 144 et 192 DPI (notifications fermé et ouvert, stockage avec origine longue, ancre en
bas d'écran, demande groupée, type inconnu) : `build/popups/more-*.png`. Le glyphe est mesuré en gris 123
sur fond 23, soit du blanc 50 % (attendu ≈ 139 avant lissage).

Une première exécution de `modal_test` a montré 3 échecs « période » (liste native, sensible au
premier plan). Ils sont sans lien avec ce lot et passaient dans les 3 autres exécutions.

## 6. Limites

* Pas de capture d'écran du popover réel dans l'E2E ; le rendu est prouvé par les PNG hors écran et les
  assertions UIA.
* Quand le paragraphe défile (bas d'écran, 200 %), aucun indicateur visuel ne signale la suite du texte.
  Le texte complet reste lisible par la molette, ↑/↓ et l'UIA.
* L'effet d'un retrait de l'autorisation 13 (Réglages, ligne « Autre ») sur des données déjà stockées
  n'est pas documenté : la copie ne le promet pas.
* La planche contexte B01 (caméra + micro, `DEZ-0`) n'a pas encore la rangée ; elle est hors lot et
  signalée à l'intégrateur.
* 100/150/200 % au sens fenêtre réelle : seul le DPI du moniteur de test a été exercé en direct ; les trois
  DPI sont couverts par les rendus hors écran.
