# Raccordements réels — plan, matrice et limites (branche VALXXII/Design-missing-elements)

État vivant, mis à jour à chaque lot. SDK WebView2 **1.0.4314.35** (`deps/webview2`), docs Microsoft Learn
(`learn.microsoft.com/microsoft-edge/webview2/reference/win32/*?view=webview2-1.0.4314.35`, mises à jour le 2026-10-19).
Base : `origin/main` 599da56 (0.3.132, #58, #60 `closeWithoutPrompt` / `exitClear`) fusionnée en `ac81314`.

Légende : **FAIT** (réel, testé) · **EN COURS** · **À FAIRE** · **LIMITE API** (prouvée, proposition) · **SANDBOX** (spécimen sans usage produit).

## Décisions du coordinateur (2026-11-10)
1. Mots de passe : option A pour les **nouveaux** enregistrements — coffre local chiffré actif **seulement complet et après revue
   sécurité indépendante** ; autosave moteur coupé seulement à ce moment. Rien n'est supprimé/extrait du profil moteur ; anciens
   remplissages et passkeys conservés par le moteur. Cohabitation expliquée honnêtement, aucune promesse de reskin du popup hybride.
2. Fenêtres : A renforcée — un processus par fenêtre normale, transactions interprocessus complètes (verrou nommé / writer unique),
   écritures atomiques + révisions ; sessions distinctes restaurées ; suppressions jamais ressuscitées ; crash/élection du primaire ;
   tests adversariaux 2 processus. Privé isolé. Aucun merge avant revue.

## Matrice composant → événement réel → effet → preuve
| # | Composant (Paper) | Événement / API réelle | Effet observable | État | Preuve |
|---|---|---|---|---|---|
| 3 | B08 Certificat client | `ICoreWebView2_5::ClientCertificateRequested` ; `MutuallyTrustedCertificates`, `SelectedCertificate`, `Cancel`, `Handled`, `GetDeferral` | Liste des vrais certificats (émetteur, expiration ; expirés désactivés) ; choix envoyé ; « Ne pas envoyer »/Échap = sans certificat ; navigation/onglet/arrêt = annulation ; deferral 1× | **FAIT** (9cdb63a) | `tests/elements_integration_test.cpp` (faux args moteur) ; e2e mTLS : **à faire** (fixture serveur + certificat client de test) |
| 2 | B07 Téléchargement dangereux | `DownloadOperation.StateChanged` + `InterruptReason` FILE_MALICIOUS / FILE_SECURITY_CHECK_FAILED / FILE_BLOCKED_BY_POLICY | B07 sous le bouton Téléchargements, vrai fichier/site/raison ; Supprimer (ligne + fichier résiduel) ; Copier le lien (URI réelle) ; Signaler (page officielle `microsoft.com/wdsi/filesubmission`) | **FAIT** (c145ba8) | intégration (fichier résiduel supprimé, menu sans « Conserver ») |
| 2 | B08 « Conserver quand même » | — | — | **LIMITE API** : `ICoreWebView2DownloadOperation` n'a ni type de danger ni override (membres : Cancel/Pause/Resume/InterruptReason…) | Proposition : le choix reste au moteur/SmartScreen ; « Conserver » uniquement en sandbox |
| 8 | A8 À propos | `UpdateCore::Engine::View()` (phases), `UpdateAction` | Notes de version (release de la version courante, dépôt public `browser-releases`), Signaler (issues publiques), statut live : recherche / disponible → Télécharger / % / prête → Redémarrer (handoff #60) / erreur → Réessayer | **FAIT** (048b864) | `tests/update-integration/host_e2e.cpp` (moteur réel, transport simulé), intégration |
| 9 | A2 blocage dialogues répétés | `ScriptDialogOpening` | Case dès le 2e dialogue d'un document ; cochée = dialogues suivants refusés ; levée à la navigation | **FAIT** (2743eae) | intégration |
| 9 | A4 beforeunload actualiser/quitter | `ScriptDialogOpening` BEFOREUNLOAD + notre Reload | « Actualiser ce site ? » quand notre rechargement déclenche l'unload | **FAIT** | intégration |
| 9 | A6 fermeture + téléchargement | `WM_CLOSE` → `CloseWindowAllowed` | Variante « Fermer et annuler le téléchargement » (fichier et % réels), toujours demandée ; « Ne plus demander » enregistré | **FAIT** | intégration |
| 9 | A6 suppression favoris, A3 InputSpec | — | — | **SANDBOX** volontaire (suppression déjà annulable par toast ; pas de confirmation intrusive) | — |
| 6 | A9 Navigation privée | `ICoreWebView2ControllerOptions::put_IsInPrivateModeEnabled` + `put_ProfileName` (ICoreWebView2Environment10) | Fenêtre privée = processus séparé, contrôleurs InPrivate, aucune écriture app (session/historique/favoris/téléchargements/coffre), puce « Privée » | **EN COURS** | — |
| 7 | A7 limite 128 → Nouvelle fenêtre | processus fenêtre + magasin transactionnel | Fenêtre normale réelle avec les URLs restantes, restaurée ; limite par fenêtre | **À FAIRE** (décision 2 A renforcée) | tests 2 processus/crash à écrire |
| 1 | B03/B04/B05 Mots de passe | aucune API coffre moteur ; `IsPasswordAutosaveEnabled` | Coffre maison DPAPI par profil/origine, consentement B04, remplissage B03 ciblé cadre/document, gestion B05 | **À FAIRE** (décision 1 A, activation après revue) | — |
| 1b | Suggestions identifiant / e-mail / passkey (popup moteur « Données enregistrées », « Utiliser la clé d'accès d'un autre appareil ») | **aucune** API WebAuthn/passkey/credential dans le SDK (0 occurrence dans `WebView2.h`) ; popup rendu par le moteur | Inchangé : passkeys et anciens remplissages restent au moteur ; Windows Hello/WebAuthn = fenêtres système | **LIMITE API** — non dessiné, non substituable | Besoin UI éventuel : aucun tant que le moteur garde ce popup |
| 4 | B08 Partage d'écran | `ScreenCaptureStarting` (Cancel, Handled, OriginalSourceFrameInfo, GetDeferral) | Le sélecteur de sources reste celui du moteur (aucune API de source) ; notre sélecteur à aperçus = sandbox | **LIMITE API** | Learn `icorewebview2screencapturestartingeventargs` |
| 5 | Puce caméra/micro « en cours » | aucun signal d'usage (seulement `IsDocumentPlayingAudio` = sortie audio) | Autorisé/bloqué réels ; « en cours » = démo | **LIMITE API** | rapport précédent + header |

## Besoins UI (pour le rédacteur Paper unique, aucune mutation par l'intégrateur)
- A9 privé : puce « Privée » + fenêtre distincte existent en concept (PrivateSpec future) ; besoin éventuel : état « fenêtre privée ouverte » de la puce dans la barre d'onglets si absent.
- A7/A6 multi-fenêtres : aucun écran nouveau requis (toast existant).
- B05 gestionnaire réel : vue liste/détail/suppression existante ; besoin éventuel : état vide « Aucun mot de passe enregistré par browser » et note de cohabitation « Les mots de passe enregistrés avant restent proposés par le moteur ».
- Suggestions hybrides identifiant/passkey : pas de design tant que le moteur conserve son popup (limite API).

## Revue indépendante obligatoire avant merge
Coffre mots de passe (chiffrement DPAPI, origine/cadre/document, consentement, aucun secret lisible par la page via le pont) et
fenêtres/privé (isolation, transactions interprocessus). Jalon signalé au coordinateur.
