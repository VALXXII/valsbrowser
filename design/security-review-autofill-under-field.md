# Security review: our lists under a page field, our validation bubble, the key pill (#121)

Scope: the three boards of ticket #121 validated by the owner (page Paper « #121 — Fenêtres du moteur à notre design »):
« Infos enregistrées · A », « Liste réunie, comptes + infos · A », « Bulle de vérification · A », and point 4
« Clé dans la barre d'adresse », variant C. Base: `main` 86697e0 (vault active, PR #122 merged). Written BEFORE any
code, as the ticket asks: showing a list under the page's own field changes the vault contract of
`design/review-final-integration.md` §5 (« the list stays anchored to the key; a page hint may only position a
secondary affordance »).

**Verdict: safe to build with the protections P1–P16 below. No point needs a new owner decision.** The owner already
chose the behaviour (a list under the field, accounts and typed info together); what remains are implementation rules,
and every residual risk (R1–R7) is Low and already present with the engine's own list and bubble today. This document
replaces the §5 sentence quoted above for the list under the field; everything else in §5 (the key, its arming, the
guards at click and at choice) stays in force and also applies to the key pill.

---

## 1. What changes

| Before (§5) | After (#121) |
|---|---|
| B03 « Comptes enregistrés pour … » opens only from our key in the address bar (chrome, out of the page's reach) and is drawn under the address bar | The same accounts, plus the e-mails the person typed before, appear **under the field the person is typing in**: the page's field decides where the list is, and typing in it opens the list |
| The engine draws its own « Informations enregistrées » list (IsGeneralAutofillEnabled) and its « Insérez un symbole @ » bubble | Both engine surfaces are replaced by ours; the engine's form autofill is turned off |
| Key: a ghost button left of « Copier le lien » | Key: a pill right after the padlock, with the number of saved accounts (variant C) |

## 2. Threats considered

A page (or a third-party frame inside it) is the attacker. It controls its DOM, its layout, its scripts, its frames and
when its fields get focus. It does not control the browser's native windows, trusted input events, or the isolated
worlds the browser creates through the DevTools protocol.

| # | Threat | How a page would try it |
|---|---|---|
| T1 | Spoofing / overlaying our list | Draw a fake list in its content; or cover ours; or make ours cover the browser's own UI (address bar, prompts) |
| T2 | Click-jacking | Make the list appear (or move) right under the pointer just before a click, so a row is chosen by accident |
| T3 | Reading suggestions before the pick | Preview into the field, focus/blur side effects, window size, events, timing; enumerate saved e-mails by typing prefixes itself |
| T4 | Opening the list without the person | Script-set values, synthetic events, `execCommand('insertText')`, autofocus |
| T5 | Cross-origin frames | A third-party frame (ad, widget) gets the list or a fill; a frame of the same site under a foreign frame |
| T6 | Filling a hidden or swapped field | Hidden / transparent / covered fields; moving focus or swapping the element between list and pick |
| T7 | Timing between list and pick | Navigate, redirect or change the document after the list opened, before the fill |
| T8 | Planting data in our memory | Make the browser store a value the person never typed (an attacker's e-mail suggested later on other sites) |
| T9 | Our bubble as a phishing surface | Trigger our native-looking bubble at will with page-chosen text |
| T10 | The key pill | Imitate it, or make it react to the page |

## 3. Protections (the contract the code must keep)

**P1. Drawn by us, outside the page.** The list and the bubble are owned popup windows of the browser window
(`WS_POPUP`, `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, D2D), never DOM. The page cannot read, style, cover or click them
with script. (T1, T3)

**P2. Inside the page area only.** The surface lies entirely inside the tab's own WebView rectangle: placed under the
field (list 8 DIP, bubble 6 DIP, left edges aligned, as the boards), moved left to stay inside, flipped above the field
when there is no room below, and not shown at all when it fits neither way. It never covers the tabs, the toolbar, the
address bar, our prompts or another split pane. (T1)

**P3. Opened only by real typing.** The list opens only after a **trusted `keydown` and a trusted `input`** on the same
field, the input within 1 s of the key, in our isolated world. Script-set values, synthetic events, `execCommand`
(its `input` is trusted in Chromium but has no key), drag-and-drop, autofocus and focus alone never open it.
Auto-repeated keys do not count. (T4, T3)

**P4. Top-level https document only.** The field must be in the top-level document: the report comes from the tab's own
DevTools session (never a frame session), the isolated world checks `window.top === window`, its `self.origin` (the
real origin, `"null"` when opaque) is a canonical https origin, `isSecureContext` holds, and the host checks it equals
the canonical origin of the tab's engine source. No list in frames, no list on http, file, data or error pages. (T5)

**P5. A real, visible, identifier field.** Checked in the isolated world when the list opens and again at the fill:
an `INPUT` of type email, or text/tel/search with an identifier hint (`autocomplete` username/email, or name/id like
mail/login/user/identifiant), not password, not disabled or read-only, connected and focused, `checkVisibility`
(opacity, visibility, content-visibility), cumulative opacity of the element and its ancestors ≥ 0.5, at least 8×8 CSS
px, entirely inside the viewport, the element itself at the hit-test point of its centre (not covered), visual viewport
not pinch-zoomed. (T6, T2)

**P6. Nothing reaches the page before the pick.** The rows exist only in the host and the native window. There is no
preview in the field, no value in the isolated world (it only learns the number of rows to route ↓/↑), the list never
takes focus (`WS_EX_NOACTIVATE`, `MA_NOACTIVATE`), so the page gets no blur. (T3)

**P7. The pick is the person's action.** A row is chosen only by (a) a mouse press **and** release on that row of our
native window, both after arming; or (b) trusted ↓/↑ then Enter **on the anchored field**, not auto-repeated, after
arming. Arming as §5: the list must have been visible, still, in the foreground window for ≥ 500 ms; any re-placement
re-arms it. A page message can never choose a row: the only channel is a DevTools binding that exists only in our
isolated world. (T2, T4)

**P8. Closed on any change.** Blur of the field, scroll, resize or move of the field (any rect change), a new document,
a navigation (`navStamp`), a tab switch, the window losing the foreground, moving or resizing, a zoom change. Opening
again needs typing again (P3). (T2, T7)

**P9. Fill re-checks everything at the pick.** The host checks: the tab is still the active tab of the foreground
window, same `navStamp`, the engine source's canonical origin is still the one the list opened for, no error page or
https interstitial, the list is visible and armed. Then:
- *an e-mail (typed info), or an account's username on an identifier-only step*: evaluated in the **same isolated world
  context** that reported the field (its execution context id comes from the engine's `Runtime.bindingCalled`, never
  from the page), on the **same element reference** kept there: still connected, focused, passing P5, its rect within
  2 px of the anchor. The value is set with that world's own `HTMLInputElement` setter, then `input`/`change`. A new
  document destroys that context: the evaluation fails and nothing is filled.
- *an account on a form with a visible password field*: the existing B03 path, unchanged (fresh isolated world,
  `FillScript` origin / ancestor / visibility checks, secret wiped, review R-1 / V1 / V2 / V4 / V7). (T6, T7)

**P10. Identifier-only step: no secret leaves.** When the field's form (or the document, for a form-less page) shows
no visible password field, choosing an account fills **the username only**: no `Reveal`, no DPAPI call, no secret in any
script. This also fixes the « Aucun champ de mot de passe visible » refusal noted in #121 for that step. (T6)

**P11. Accounts only for the exact origin, never in private.** Account rows are the vault's entries for the canonical
origin equal to the top document's and to the engine source (as B03). The list never decrypts anything (rows carry the
username only). Private windows show no list under the field (the vault there stays read-only behind the key, D3) and
never store typed info. (T5, T8)

**P12. Typed info: only what the person typed and sent.** Stored in our own file `<profile>\form-entries` through the
profile-store transaction (lock, atomic replace, three-way merge, a damaged or unknown file is never rewritten). A value
is stored only when: e-mail shaped (one `@`, a dot in the domain, ≤ 254 characters, no space, control, line-separator
or text-direction character); typed by the person (the field's value at the action equals the last trusted-typed
value, as the vault's capture V-4); in a visible field of a top-level https document; on a trusted submit or a trusted
click / Enter on a button-like element; never from `autocomplete="off"`, `one-time-code` or password fields; never in a
private window. At most 500 entries (the least recently used goes first). A value picked from our list only updates its
date. Plain text, like the history (e-mails are not secrets). (T8)

**P13. The engine's list is off, its data kept.** `IsGeneralAutofillEnabled` is FALSE whenever our list runs (always,
« Saisie automatique » on or off), so there are never two lists for typed info. The engine's saved form data is
**never deleted automatically and never written**. Once, the e-mail-shaped values of the engine's own `autofill` table
(`<engine profile>\Web Data`; never `Login Data`, nothing decrypted) are copied into our file so nothing the person
typed before is lost: through the existing private-snapshot reader (`src/browser-import.h`: the source opened without
write sharing, so only while the engine is not running; the copy is queried read-only and deleted). If the engine holds
the file, it is retried at the next start. It is erased with the engine's form data (clear on exit, full clear), and per
row with « retirer ».

**P14. Our bubble: no action, our words.** The isolated world cancels the engine's bubble (`preventDefault` on the
`invalid` event) only in the top-level document and only when the page did not cancel it itself (a listener appended to
the target during the dispatch runs last). Our bubble then shows **our fixed message** for the validity state; only a
`setCustomValidity` message (as the engine shows today) is the page's text, cut to one line of at most 150 characters
without control, line-separator or text-direction characters. The bubble is click-through, has no button, hides after
5 s, on the next input, on blur, scroll, navigation or tab switch, and follows P2. In frames the engine's bubble stays.
(T9)

**P15. The key pill is chrome.** It is drawn in the address bar, where a page cannot draw; its number comes from the
vault for the engine's canonical origin, never from the page; its activation keeps §5 (armed 500 ms, foreground, press
and release after arming, source re-read at click and at choice). (T10)

**P16. Capture world unchanged.** The vault's capture world, binding and frame auto-attach (PR #122) are not changed.
The forms features use their own isolated world (`browser-forms`) and binding (`browserForms`) in the tab's own session
only; turning « Mots de passe » off removes the vault's world but keeps the `Page` domain on for the forms world.

## 3b. Update for the validated states (board « Listes sous le champ + bulle · états », 24 November)

Re-checked against P1–P16; nothing needs a new owner decision.

- **Keyboard: ↓ then Entrée, Suppr.** Suppr removes the e-mail of the row chosen with ↓ from our file. It is routed like
  ↓ / Entrée (P7): only a trusted, non-repeated key on the anchored field, only while the host has told the world a list
  is shown, never reaching the page; the host accepts it only for an INFO row, only once the list is armed (500 ms), for
  the same tab, document and list (sequence number). A page cannot remove anything: it cannot forge the key, and the
  binding exists only in our world. Residual: R2 now also covers Suppr (the page can notice that the key did not reach
  it, never which e-mail).
- **List above the field** when there is no room below: already P2 (inside the page area, never over the chrome);
  re-placement re-arms (P7).
- **Key pill hovered, next to a B01 request pill**: chrome only, no new input path (P15).
- **The identifier-only step through the key** (P10 extended): `VaultFillChosen` (key and list) first runs
  `Vault::UserStepScript` in a fresh isolated world. It holds NO secret, re-checks origin / secure context / ancestors
  before writing, answers « password » when the focused form shows a visible password field, else fills the account's
  username into a visible, writable identifier field. Only on « password » are the guards checked again, the secret
  decrypted and `FillScript` run (unchanged). So on an identifier step no secret is decrypted or sent at all.
- **Bubble**: 16 fixed messages of the board, the page's own text still cut to one line of 150 characters (P14).

## 4. Residual risks (accepted, Low)

| # | Risk | Why it is acceptable |
|---|---|---|
| R1 | A page can draw an imitation of our list or bubble inside its own content | True of any UI in page territory, and of the engine's own list and bubble today; the address bar stays the trusted indicator; an imitation obtains nothing from the vault |
| R2 | One-bit inference: when the person presses ↓, ↑, Enter or Échap on the field, or moves the pointer onto the list, the page can notice that the keys did not reach it or that the pointer left it, so that « a suggestion existed for what the person typed » | Needs the person's own action; never a value; the engine's list behaves the same |
| R3 | A page the person lets capture the screen sees the list like any window | Same for all native UI |
| R4 | Sites where the engine still holds an old password (option A) can still show the engine's password list on focus | The engine shows it even with autosave off; it disappears once the person accepts the 30-day deletion offer (Réglages › Mots de passe); our list cannot suppress it |
| R5 | A page can stop our listeners (`document.open`, `stopImmediatePropagation` on the target) | Fails safe: no list, the engine's bubble comes back |
| R6 | Typed e-mails are stored in clear | Not secrets; same as the history; erasable |
| R7 | The page triggers our bubble at will (`reportValidity`) | No action in it, our fixed words except a short `setCustomValidity` line, inside the page area, as the engine's bubble |

## 5. Tests that must exist (windowless)

- Store: e-mail shape and character refusals, LRU limit, merge (removal here applied, another window's never undone),
  damaged / unknown file never rewritten, private writes refused, engine import (fixture SQLite `autofill` table: only
  e-mail values, never Login Data, source never written, busy source retried).
- Report parsing: only our binding name, only top, only canonical https equal to the engine source, bounded sizes,
  unknown kinds refused.
- Placement: inside the page area, flip above, refusal when it fits nowhere, never above the page's top edge.
- Arming: no pick before 500 ms, a press before arming is ignored, re-placement re-arms.
- Rows: accounts only for the exact origin, no duplicate (an e-mail that is an account is not repeated in the infos),
  prefix filter, never a secret in a row.
- Scripts: the forms world checks `isTrusted`, `window.top`, `self.origin`; the fill re-checks focus, visibility and
  rect; the username-only fill carries no secret; the bubble listener cancels only when the page did not.
- Engine: `IsGeneralAutofillEnabled` FALSE in every window; no ClearBrowsingData of autofill outside the person's clear.
- Key pill: count from the vault, after the padlock, armed activation.

The real-window checks (list and bubble pixels against the boards, keyboard path, a hostile test page trying T1–T9)
are scripted for the night run (`tests/e2e/autofill_qa.ps1`).

---

## Corrections (intégrateur) — mise en œuvre, branche `VALXXII/121-autofill`

| # | État | Où | Preuve |
|---|---|---|---|
| P1 | Fait | `src/field-list.h`, `src/validation-bubble.h` : fenêtres à nous (`WS_EX_NOACTIVATE`, bulle `WS_EX_TRANSPARENT` + `HTTRANSPARENT`), dessinées en D2D | U `test-form-memory` (rendus hors écran comparés aux exports Paper) ; nuit `tests/e2e/autofill_qa.ps1` |
| P2 | Fait | `Forms::Place` (sous le champ, décalée à gauche, au-dessus si pas de place, sinon rien), rectangle de la vue de l'onglet | U « Placement » (6 cas) |
| P3 | Fait | monde `browser-forms` : touche fiable puis `input` fiable du même champ en moins d'1 s | U (texte du script) ; nuit T4 (`execCommand`) |
| P4 | Fait | `window.top===window`, `self.origin`, session de l'onglet seulement (`FormsEvent` refuse une session de cadre), origine = source moteur | U ; nuit T5 (cadre d'une autre origine) |
| P5 | Fait | `rectOf` du script (visibilité, opacité cumulée ≥ 0,5, test de recouvrement, pas de zoom pincé) | U ; nuit T6 (champ presque transparent) |
| P6 | Fait | aucune prévisualisation, `MA_NOACTIVATE`, le monde ne reçoit que le nombre de lignes | nuit (touches de la page comptées) |
| P7 | Fait | `Forms::Armed` 500 ms + premier plan ; appui ET relâchement armés ; ré-armement à chaque re-placement | U ; nuit (clic trop tôt sans effet) |
| P8 | Fait | monde : blur, scroll, resize, déplacement (rAF), Échap, Tab, pagehide ; hôte : `FormsSync` (onglet, navigation, vue), `WM_SIZE`/`WM_MOVE`/désactivation | nuit (champ qui bouge, T2/T7) |
| P9 | Fait | `FormsPick` (onglet, `navStamp`, origine moteur, armement) puis `__bf.fill` dans le MÊME contexte (id de contexte venu du moteur) sur le MÊME élément ; compte + mot de passe : `VaultFillChosen` (= chemin B03 inchangé, extrait d'`OfferVaultFill`) | U (réponses du monde) ; nuit |
| P10 | Fait | étape identifiant seule : le nom d'utilisateur seul, sans `Reveal` | nuit (log « identifier step, no secret ») |
| P11 | Fait | comptes de l'origine exacte (`vault.For`), aucun déchiffrement pour la liste ; fenêtre privée : ni liste ni enregistrement (`Forms::WritesForbidden`) | U |
| P12 | Fait | `Forms::Store` (`<profil>\form-entries`, en-tête, condensat, fusion à 3, fichier abîmé jamais réécrit, 500 max), seulement les e-mails tapés et envoyés | U (stockage, fusion, dommage, privé) ; nuit T8 (e-mail planté) |
| P13 | Fait | `IsGeneralAutofillEnabled` toujours FALSE ; copie unique des e-mails de la table `autofill` du moteur via `BrowserImport::Snapshot` (jamais `Login Data`, rien supprimé) ; effacés avec « Effacer les données » complet / à la fermeture | U (fixture SQLite : seulement les e-mails, source jamais écrite, Busy quand le moteur tient le fichier) |
| P14 | Fait | `invalid` annulé seulement si la page ne l'a pas fait ; nos mots (`Forms::InvalidText`), texte de la page coupé à 150 caractères sur une ligne | U ; nuit (`/own-validation`, T9) |
| P15 | Fait | pastille dessinée dans la barre (`chrome.h`), nombre lu dans le coffre ; armement §5 inchangé | nuit (pastille, clic) |
| Moteur réel | Fait | `tests/forms-world/run.py` : le script du monde isolé dans le vrai moteur, fenêtre jamais affichée (jour), installé comme `FormsInstall`, frappe et clics par le domaine Input | 22 contrôles PASS : P3 (frappe réelle, `execCommand` refusé), P4 (cadre d'une autre origine), P5 (champ presque transparent), P6/P7 (↓ Entrée seulement quand l'hôte a montré une liste, jamais reçus par la page, pas d'envoi du formulaire), P8 (champ qui bouge, Échap), P9 (remplissage du même élément, second remplissage refusé), P12 (e-mail tapé envoyé, e-mail planté jamais), P14 (bulle du moteur annulée, validation propre à la page gardée, texte de la page coupé) |
| États (24 nov.) | Fait | Suppr au clavier (`del`, armé, ligne d'e-mail seulement), liste au-dessus (P2), pastille survolée, étape identifiant par la clé (`UserStepScript`, aucun secret) | U `test-form-memory` (93, rendus des états comparés à Paper), `test-password-vault` (forme du script), moteur réel `tests/forms-world` (34 : Suppr jamais reçu par la page, identifiant seul rempli, formulaire avec mot de passe → « password », champ de recherche refusé, autre origine refusée) |
| P16 | Fait | monde et liaison à part ; `VaultCaptureEvent` ignore la liaison des formulaires ; « Mots de passe » coupé ne désactive plus le domaine `Page` si le monde des formulaires est là | U `test-elements-integration` |
