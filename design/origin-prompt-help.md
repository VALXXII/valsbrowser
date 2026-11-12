# Aide contextuelle des demandes de site — inventaire, contrat, plan

Base code : `8237c2a` (VALXXII/Design-missing-elements, PR63, committé ; aucun fichier WIP lu comme contrat).
Base main : `eb1a943` (0.3.137). Lot UI/copie uniquement : aucune politique de sécurité, de mémorisation
ou d'activation n'est changée.

## 1. Cause vérifiée de la capture x.com

Capture : popover « x.com demande une autorisation », icône info, case « Mémoriser pour ce site ».

* Journal local de la 0.3.137 (`%LOCALAPPDATA%\browser\data\browser.log`, filtré sur les seules lignes
  `PERMISSION request kind=` / `PERMISSION kind=`, rien d'autre lu) :
  `PERMISSION request kind=13 https://x.com` puis `PERMISSION kind=13 action=0` (fermé, rien enregistré).
* SDK local 1.0.4314.35 (`WebView2.h` l. 2724-2737) : `13 = COREWEBVIEW2_PERMISSION_KIND_PERSISTENT_STORAGE`.
  Microsoft : « Indicates permission to persist storage created by the Storage APIs, service workers, etc. »
* `OriginPrompt::Permission` ne connaît que 0-12 : `Title()` tombe dans `default` (« demande une
  autorisation »), `Subtitle()` vide, `Glyph()` info, et la case générique s'affiche.

**Ce n'était donc pas une notification** (le chemin Notifications, avec son titre propre et sa note
« toujours mémorisé », n'est pas celui de la capture). C'est `navigator.storage.persist()` : la page demande
que ses données locales ne soient plus effaçables automatiquement par le navigateur (WHATWG Storage
Standard : stockage « best effort » évictable sous pression, « persistent » effacé seulement par l'utilisateur).

## 2. Faits moteur retenus (sources primaires)

| Fait | Source | Effet sur la copie |
|---|---|---|
| `SavesInProfile` TRUE : l'état « persists across sessions and becomes the new default » ; FALSE : « not persist the state beyond the current request » | `ICoreWebView2PermissionRequestedEventArgs3` | « Mémoriser » coché = enregistré dans ce profil ; décoché = cette demande seulement |
| Fermer/Échap = `deny(false)` | `elements-integration.h` `PermissionRequested` | « Fermer refuse cette fois, sans rien enregistrer » |
| Notifications : `remember` forcé (politique existante, inchangée) | `OriginPrompt::Map` | note « Choix toujours mémorisé » conservée |
| Notifications non gérées par l'hôte (`NotificationReceived` non abonné) : WebView2 « will display the default notification UI » ; l'événement couvre les notifications « non-persistent » | `ICoreWebView2_24`, `…NotificationReceivedEventArgs` | **retrait de « même onglet fermé »** : aucune preuve moteur/lifecycle (push/service worker non documentés pour WebView2) |
| InPrivate : profil dédié, durée de conservation des autorisations non documentée | `ICoreWebView2ControllerOptions` | aucune promesse de durée ; « dans le profil de cette fenêtre » reste vrai dans les deux cas |
| Réglages › Autorisations des sites relit `GetNonDefaultPermissionSettings` (types hors liste regroupés « Autre ») | `main.cpp` `QueryPermissions` / `SetSitePermission` | « Modifiable dans Réglages › Autorisations des sites » vrai pour tous les types |

## 3. Matrice des demandes réelles (8237c2a)

| Dialogue | Réel ? | Nature / origine / conséquence visibles | Manque | Décision |
|---|---|---|---|---|
| Permission 1 type connu (caméra, micro, position, presse-papiers, MIDI, téléchargements) | oui | titre « <site> veut … », sous-titre pour 4/12 | portée (origine exacte), effet de « Mémoriser », effet de la fermeture, révocation | **Lot 1** : sous-titre court partout + « Plus d'infos » |
| Permission notifications | oui | titre spécifique + note « toujours mémorisé » | « même onglet fermé » non prouvé | **Lot 1** : copie corrigée + « Plus d'infos » |
| Permission 13 stockage persistant | oui (x.com) | **générique, faux silence** | nature absente | **Lot 1** : titre/sous-titre/glyphe propres |
| Permission 0 inconnue / >13 futur | oui | générique | ne dit pas que le type est inconnu | **Lot 1** : « type non précisé par le moteur » / « non reconnu (code n) » |
| Permission caméra + micro groupés | oui | rangées par type | portée/mémorisation | **Lot 1** : même « Plus d'infos » (groupement inchangé) |
| SiteStatus (puce) | oui | rangées + détail par rangée | — | aucun changement |
| Authentification HTTP | oui | origine, realm cité, note HTTP | — (nature claire, rien mémorisé) | aucun changement |
| Application externe (site / cadre tiers) | oui | origine, schéma, note cadre tiers | — | aucun changement |
| Téléchargement dangereux / KeepFile | oui | carte danger, raison moteur | — (contrat B07/décision 4 récent) | aucun changement |
| Certificat client | oui (E1) | origine, liste, Envoyer/Ne pas envoyer | — | aucun changement (contrat E1 récent) |
| Mots de passe (coffre) | **inactif** | — | contrat non stabilisé (revue F1-F6) | **attente** : inventaire seulement |
| Partage d'écran | démonstration | badge Démonstration | — | aucun changement |
| Dialogues JS, fermeture, effacer données, privé | oui | texte du site / conséquence en corps | — | aucun changement |

L'audit actuel n'a trouvé d'information manquante que dans les demandes de permission (nature absente pour
13/0, portée, effet de « Mémoriser » et de la fermeture invisibles). Les autres dialogues réels disent déjà
nature, origine et conséquence (suppression, confirmation, envoi) ; leur ajouter « Plus d'infos » serait
mécanique. Le coffre sera revu une fois son contrat stabilisé.

## 4. Contrat visuel « Plus d'infos » (popover ancré B01/B02, 340 DIP)

Principe repris des rangées utilisateur A10 (`QL8/QKI/QJW`, rangée `QKP-0`), pas leurs paragraphes :

* ordre : tête (glyphe 16 + titre 16/20 Medium + sous-titre 14/20 70 %) → rangée « Plus d'infos » →
  [paragraphe si ouvert] → case « Mémoriser » ou note → Bloquer / Autoriser ;
* rangée : h 32, rayon 6, écart 8, libellé 14/20 Regular `#FFFFFFE6` (flex 1) puis icône plus 14 à 50 % à droite ;
  **repos** gauche 0 / droite 4 (`QLE-0`) ; **survol** fond `color-mix(#FFF var(--opacity-browser-focus))`,
  inline 8 (`QKP-0`, resserrement voulu) ; **ouvert** gauche 8 / droite 4, icône tournée 45° (`QK3-0`) ;
  **focus clavier** anneau standard du dialogue ;
* paragraphe ouvert : 14/20 `#FFFFFFB3`, inset 8, collé sous la rangée (`QL7-0`), hauteur selon le texte (jamais
  rogné ; le popover grandit, l'ancrage ne bouge pas) ;
* fermé par défaut à chaque demande ; l'état ne survit pas à la demande.

## 5. Contrat de comportement

* « Plus d'infos » n'accorde ni ne refuse rien : bascule seulement, `done` n'est pas appelé, la
  deferral reste en attente ; Échap ferme la demande (Dismiss) comme avant, même ouvert.
* Entrée / Espace sur la rangée basculent ; Tab : rangée → case → Bloquer → Autoriser → Fermer.
  Le focus initial ne change pas (Bloquer/Autoriser restent sans défaut forcé, comme 8237c2a).
* UIA : bouton « Plus d'infos », motif ExpandCollapse (Collapsed/Expanded), `AutomationId`
  `PermissionMore` ; ouvert, un nœud Texte porte le paragraphe ; le groupe garde le sous-titre en aide.
* Aucun changement : deferral exactly once, origine moteur du demandeur, navigation/fermeture/onglet,
  groupement caméra+micro, mémorisation (case, notifications forcées), privé.

## 6. Copies (FR)

Sous-titres (une phrase, visible) :

| Type | Titre | Sous-titre |
|---|---|---|
| Caméra | « <o> veut utiliser votre caméra » | Le site pourra filmer avec votre caméra. |
| Micro | « <o> veut utiliser votre micro » | Le site pourra enregistrer le son de votre micro. |
| Position | inchangé | inchangé |
| Notifications | inchangé | Le site pourra afficher des alertes en dehors de la page. |
| Capteurs | inchangé | Accéléromètre, gyroscope et autres capteurs de l'appareil. |
| Presse-papiers, téléchargements multiples, MIDI | inchangés | inchangés |
| Fichiers | inchangé | Lecture et écriture de fichiers ou de dossiers de cet appareil. |
| Lecture automatique | inchangé | Le son et la vidéo pourront démarrer sans clic de votre part. |
| Polices | inchangé | Le site verra les polices installées sur cet appareil. |
| Fenêtres | inchangé | Le site pourra ouvrir et placer des fenêtres sur vos écrans. |
| **13 Stockage persistant** | « <o> veut conserver ses données sur cet appareil » | Sinon, le navigateur peut les effacer s'il manque d'espace. |
| 0 inconnu | « <o> demande une autorisation » | Le moteur ne précise pas de quoi il s'agit. |
| > 13 | « <o> demande une autorisation » | Type non reconnu par ce navigateur (code moteur n). |

Paragraphe « Plus d'infos » = détail du type (si utile) + portée commune :

* Notifications : « Elles peuvent apparaître en dehors de cette page. Leur réception dépend du site et du
  navigateur : rien ne garantit qu'elles arrivent onglet ou navigateur fermé. » (preuves techniques :
  §2 et rapport, pas dans la copie).
* Stockage persistant : « Autoriser protège les données que le site enregistre ici contre l'effacement
  automatique quand l'appareil manque d'espace. Vous pouvez toujours les effacer vous‑même ; Bloquer ne
  supprime rien. » (pas de promesse contre une politique, l'OS ou un effacement volontaire ; l'autorisation
  est réglable dans Réglages › Autorisations des sites, ligne « Autre » — vérifié `SetSitePermission` /
  `PermissionName` ; l'effet d'un retrait sur des données déjà stockées n'est pas documenté, donc non promis).
* Portée commune : « Concerne <origine complète> uniquement. » + (case) « Sans « Mémoriser », votre choix
  ne vaut que pour cette demande ; coché, il est enregistré dans le profil de cette fenêtre. » ou
  (notifications) « Votre choix est enregistré dans le profil de cette fenêtre. » + « Modifiable dans
  Réglages › Autorisations des sites. Fermer refuse cette fois, sans rien enregistrer. »

## 7. Fichiers prévus (patch ciblé, aucun reformatage)

* `src/origin-prompt.h` : `Permission::PersistentStorage = 13`, `Text::Title/Subtitle/Glyph/Noun`,
  `Text::More()`, `Spec::requester` (origine complète) ; `Build` remplit `m.more`.
* `src/modal.h` : `Spec::more {label, text, open}`, `B::More`, `MoreHit`, layout/paint/hit/clavier/UIA,
  `MoreOpenForTest()`. Aucun autre bloc modifié.
* `src/site-request.h` : glyphe stockage (chip), `Kind::PersistentStorage`.
* `src/elements-integration.h` : `spec.requester = OriginOf(uri)` (une ligne) ; journal inchangé.
* tests : `tests/modal_test.cpp` / `origin_prompt` existants étendus.

## 8. Plan

1. Paper (`p-1-0` section permissions B01/B02 + copie lecture `p-F-0`) : variantes repos/survol/ouvert/focus
   notifications, stockage persistant ouvert, fallback inconnu. Icon/tokens/fils vérifiés avant/après.
2. Code selon §4-7, tests unitaires (mapping 0/4/13/14, copies, bascule sans résolution, clavier, UIA,
   hauteur DPI/texte long), build estampillé, fixture locale isolée (`navigator.storage.persist()`,
   `Notification.requestPermission()`) sur profil jetable.
3. Commit(s) UI distincts, PR vers `VALXXII/Design-missing-elements` (adoption par l'intégrateur), sans merge.
