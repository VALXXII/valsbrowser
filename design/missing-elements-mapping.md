# Éléments & popups — mapping Paper → code et inventaire

Source : Paper « Browser Design » (`01M6Q7YT14ZKMGET54ASPNGQEC`), page `p-D-0`, 28 artboards, lus le
2026-11-09 via `get_jsx` / `get_computed_styles` (aucune valeur relevée sur capture, Paper non modifié).
Valeurs détaillées, nœuds lus et écarts : [lot A](missing-elements-A.md) (A1–A9, B01–B09, ombres) et
[lot B](missing-elements-B.md) (C0–C9, A7, sandbox). Contrats d'API : [contracts](missing-elements-contracts.md).

## Architecture

| Couche | Fichier | Rôle |
| --- | --- | --- |
| Fenêtres sans ombre | `src/native-ui.h` | `ConfigureShadowlessPopup` / `IsShadowless` / `StripTooltipShadow` appliqués à toutes nos popups |
| Modale A1–A9 | `src/modal.h` | `Modal::Dialog` + préréglages, voile, clavier, UIA |
| Popovers d'origine B01–B08 | `src/origin-prompt.h` | autorisations, auth HTTP, application externe, téléchargement, concepts |
| Démo B05 | `src/password-demo.h` | gestion des mots de passe, comptes fictifs en mémoire |
| C1/C2 | `src/find-bar.h` | barre de recherche native |
| C3/C4 | `src/error-pages.h` | pages d'erreur réseau (document interne, jeton) |
| A7/C8/C9 | `src/context-toast.h` | toasts contextuels |
| C5 | `src/tooltip.h`, `src/shortcuts-panel.h` | info-bulle Paper (barre d'outils réelle), panneau Ctrl+/ |
| C6/C7 | `src/fullscreen-hint.h` | indication de sortie du plein écran |
| Sandbox | `src/debug-sandbox.h` | page `browser://debug`, catalogue, allowlist du pont |
| Intégration | `src/elements-integration.h` (+ `src/main.cpp`) | plomberie WebView2, files de requêtes, deferrals, sandbox |

## Inventaire honnête

### Intégré (composant de production sur le vrai chemin)

| Artboard | Branchement réel |
| --- | --- |
| A2 alert / confirm, A3 prompt, A4 beforeunload | `ICoreWebView2::add_ScriptDialogOpening` + `put_AreDefaultScriptDialogsEnabled(FALSE)` ; deferral complété une fois ; case « empêcher d'autres dialogues » dès le 2ᵉ dialogue du même site |
| A5 effacer les données | Réglages → vraie modale ; catégories + période → `ICoreWebView2Profile2::ClearBrowsingDataInTimeRange` + traces locales de la même période ; état « Effacement… », erreur affichée si échec |
| A6 fermer la fenêtre, ouvrir beaucoup de favoris | `WM_CLOSE` (≥ 2 onglets, réglage existant) asynchrone ; « Ouvrir tout » > 20 favoris |
| A7 limite de 128 onglets | `NewTab` à 128 → toast (remplace la MessageBox) |
| A8 À propos | menu → modale ; en preview l'état dit « Mises à jour désactivées » (pas de faux « à jour ») |
| A9 navigation privée | vraie fenêtre privée (`IsInPrivateModeEnabled`, profil `browser-private`, même exécutable `--private`), puce privée ; rien d'écrit sauf actions volontaires D3 (favoris, réglages) ; voir [real-integration-plan](real-integration-plan.md) |
| B01/B02 autorisations | `add_PermissionRequested` + `Args2::put_Handled` + `Args3::put_SavesInProfile` ; caméra + micro d'une même origine fusionnés ; rien d'accordé sans réponse (refus non mémorisé) |
| B06 auth HTTP Basic | `ICoreWebView2_10::add_BasicAuthenticationRequested` ; mot de passe effacé (`SecureZeroMemory`) après usage, jamais journalisé |
| B06 application externe | `ICoreWebView2_18::add_LaunchingExternalUriScheme` ; lancement moteur annulé, `ShellExecute` seulement sur « Ouvrir » ; liste de schémas refusés (file, javascript, ms-msdt, search-ms…) |
| C1/C2 Ctrl+F | `ICoreWebView2_28::get_Find`, `SuppressDefaultFindDialog`, compteurs `ActiveMatchIndexChanged`/`MatchCountChanged`, F3/Maj+F3 |
| C3/C4 erreurs réseau | `NavigationCompleted` échouée (dernière navigation http(s) de l'onglet uniquement) → document interne ; Réessayer, reprise sur l'événement `online`, compte à rebours pour le délai dépassé ; l'adresse reste l'URL en échec |
| C5 info-bulles | barre d'outils principale (libellé + raccourci) ; panneau Ctrl+/ |
| C6/C7 plein écran | `add_ContainsFullScreenElementChanged` + F11 ; fenêtre au moniteur, chrome masqué, indication Paper ; Échap/F11 quittent |

### Raccordés depuis (lot « real integration », état et preuves : [real-integration-plan](real-integration-plan.md))

A7 limite de 128 onglets → nouvelle fenêtre réelle, sessions par fenêtre · A8 états réels du moteur de mise à jour ·
A9 fenêtre privée réelle · B07 téléchargement bloqué (raison moteur réelle ; E2E non concluant) · B08 certificat client
réel (`ClientCertificateRequested`, E2E mTLS) · page TLS native pour un certificat serveur refusé (CANCEL, aucun contournement) ·
B03/B04/B05 coffre DPAPI **présent mais INACTIF en dev/release** (`Vault::Active()` faux ; actif seulement dans l'hôte de
test E2E synthétique) : tant qu'il est inactif, le moteur garde son propre comportement d'enregistrement.

### Démonstration (sandbox, données fictives, rien d'enregistré)

A1 variantes (danger, désactivé, erreur, occupé) · A3 InputSpec · A6 supprimer des favoris · A9 concept ·
B03/B04/B05 démo (la sandbox ; le chemin réel est inactif) · B08 « Conserver quand même » (limite API) · B08 partage
d'écran (sélecteur du moteur conservé) · voyant caméra/micro « en cours » (aucun signal d'usage) · C8/C9 toasts d'état et
« Page enregistrée » (pas de fonction d'enregistrement de page).

### Natif conservé

Sélecteurs de fichiers, aperçu d'impression, Windows Hello / identifiants système, centre de
notifications, partage Windows, sélecteur de partage d'écran du moteur, suggestions identifiant/passkey du moteur (aucune API), `Fail()` (erreur fatale
WebView2, MessageBox système volontairement conservée). Leurs ombres ne sont pas les nôtres.

## Zéro ombre — compromis mesuré

Sous Windows 11 26200, tout arrondi DWM (`DWMWCP_ROUND`/`ROUNDSMALL`) produit l'ombre quelle que soit la
politique NC. Nos popups utilisent donc `DWMWCP_DONOTROUND` + région GDI arrondie (bord 1 bit, sans
anticrénelage) ; la fenêtre principale garde son cadre. Aucun réglage système, DWM reste actif.
Détails et mesures : [lot A §1](missing-elements-A.md).
