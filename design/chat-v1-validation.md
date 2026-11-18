# Chat intégré V1 — validation (C4)

Nuit du 17 au 18 novembre 2026. Branche `VALXXII/annotations-chat`, build `build/chat-preview` (0.3.412, commit 81ca274).

## En bref

**Ce qui marche, vérifié dans le vrai navigateur :**

- Le chat s'ouvre à droite de la page en un quart de seconde, la page se resserre, et tout disparaît quand on
  le ferme : après fermeture il ne reste ni processus d'agent ni WebView du chat.
- Le parcours des planches 6.8 → 6.12 est rejoué en entier avec le faux agent : viser le bouton, écrire,
  carte d'autorisation, vraie modification du fichier, page rechargée, bouton encadré avec une coche,
  « Voir les changements », « Tout annuler », file d'attente, Arrêter, mode Planifier, historique, reprise
  après redémarrage. **82 étapes sur 82.**
- **Un essai réel avec Claude Code** (une seule petite demande, dans la copie du bac à sable) : Claude démarrait
  en mode « auto », le navigateur l'a mis en « Demander » avant d'envoyer ; une carte d'autorisation ; le fichier
  a été modifié ; la page s'est rechargée ; « Tout annuler » a remis le fichier. 17 étapes sur 17.
- Vos vraies configurations de Claude et de Codex n'ont pas changé (empreintes identiques avant et après).

**Ce qui manque ou n'a pas pu être vérifié :**

- L'ordinateur était verrouillé pendant la validation : dans les captures, la barre du haut (onglets, adresse)
  est noire, et le panneau de note natif aussi. Le reste des captures est réel. À refaire session ouverte.
- Aucun lecteur d'écran réel (Narrateur) n'a été essayé ; la structure d'accessibilité, elle, est vérifiée.
- Codex n'a pas eu d'essai réel (un seul essai réel était autorisé, fait avec Claude Code).
- Quatre suites du dépôt échouent, **aucune à cause du chat** (détail plus bas).

## Livrables

| Quoi | Où |
| --- | --- |
| Navigateur d'essai | `C:\Users\<user>\orca\workspaces\Browser\annotations-chat\build\chat-preview\browser.exe` |
| Bac à sable | `C:\Users\<user>\orca\workspaces\Browser\annotations-chat\sandbox\chat\` (mode d'emploi : `README.md`) |
| Commande unique | `python sandbox/chat/run-sandbox.py --fake-agent` (sans l'option : vos vrais agents) |
| Captures et rapports | `build\chat-qa\` |
| Explication technique | `design/chat-integration.md` |

Build : `python tools/release/build.py --out build/chat-preview --channel dev --preview` → **0 avertissement**
(`build/chat-build.log`). Le faux agent est construit à part dans `build/chat-preview-test/` ; les fichiers de
test du panneau (`dev.html`, `dev-host.js`) ne sont pas dans le build.

## Parcours sur le faux agent, comparé aux planches

Commande : `python tests/chat/chat_qa.py run` → `build/chat-qa/report.json` (82 étapes, 0 échec), rejoué quatre
fois de suite avec le même résultat. Le faux agent modifie réellement `styles/checkout.css` de la copie du site.

| Planche | Capture | Ce qui est vérifié | Écart avec la planche |
| --- | --- | --- | --- |
| 6.8 Viser un élément | `6.8-viser.png` | mode Annotation, cadre de survol, bouton « Demander à l'agent » dans le bandeau | le bandeau porte un bouton en plus (voulu : c'est l'entrée du chat) |
| 6.2 Première fois | `6.2-dossier.png` | dossier trouvé par le port du site, « Utiliser », agents « Installé » | — |
| 6.8 Élément visé | `6.8-element-vise.png` | puce dans la saisie, `index.html:25` en en-tête, viseur allumé | deux suggestions au lieu de trois (pas d'annotation en attente dans cet essai) |
| 6.9 Écrire la demande | `6.9-demande.png` | texte + puce, modèle et effort venus de l'agent | modèles du faux agent |
| 6.1 / 6.10 Autorisation | `6.10-autorisation.png` | étiquette « ⟳ titre - modèle » sur l'élément, carte « Claude veut modifier checkout.css », diff, rien d'écrit avant l'accord | — |
| 6.10 Claude travaille | `6.10-travaille.png` | texte en flux, bouton Arrêter, « Ajouter une suite… » | — |
| 6.11 C'est fait | `6.11-fait.png` | page rechargée, bouton déplacé, cadre + coche, « 1 fichier modifié +1 −1 », Tout annuler | — |
| 6.11 (suite) | `6.11-fichiers-deplies.png`, `6.11-voir-les-changements.png`, `6.11-annule.png` | diff sous la barre, vue « Changements », fichier remis et page comme avant | — |
| 6.12 Mode Planifier | `6.12-planifier.png` | carte « Plan proposé · 3 étapes », Lancer le plan / Modifier, rien d'écrit | — |
| 6.5 File d'attente | `6.5-file-attente.png` | message en file pendant un tour, parti après l'arrêt | — |
| 6.7 Historique | `6.7-historique.png`, `6.7-reprise.png` | conversations du site, réouverture, aussi après redémarrage | — |
| Site non local | `6.x-site-non-local.png` | panneau désactivé avec l'explication, saisie grisée, rien ne part | le rectangle noir au milieu est un défaut de capture (fenêtre de note cachée, session verrouillée) |
| Note → chat | `2.1-note-demander.png`, `6.a-note-ask-agent.png` | « Demander à l'agent » dans le panneau de note ouvre le chat avec l'élément et le texte | la première capture est noire (session verrouillée) ; la seconde est le rendu hors écran |
| Bandeau | `6.b-band-ask-agent-open.png`, `6.b2-…-reminder-focus.png` | bouton allumé quand le panneau est ouvert, focus clavier | — |

Autres vérifications du même parcours : la page passe de 1 176 à 796 px de large à l'ouverture et revient à
1 176 à la fermeture ; le lanceur d'annotations se masque puis revient ; fichier:ligne Astro = la vraie ligne
du fichier ; fichier:ligne React (`CartButton.jsx:6:5`) ; **aucun** fichier:ligne pour un élément sans
information (jamais l'attribut d'un parent) ; l'annotation est écrite, réservée, puis terminée ou rendue ;
`Échap` et `Ctrl+Maj+D` ferment ; après redémarrage le dossier est retenu.

## Mesures

| Mesure | Résultat | Remarque |
| --- | --- | --- |
| Ouverture du panneau, première fois | prêt en **200 à 318 ms** (WebView créée en 34 à 105 ms, panneau en place en 223 à 305 ms) | 5 passages |
| Ouverture suivante (WebView recréée) | prêt en **215 à 233 ms** | |
| Premier texte de l'agent affiché | **1 à 35 ms** après l'envoi avec le faux agent | temps propre au navigateur |
| Idem avec Claude Code | **8,8 s** après l'envoi (tour complet 18,1 s, dont l'attente de mon clic « Autoriser ») | temps de Claude |
| Première utilisation réelle | session prête **9,5 s** après l'envoi, téléchargement du Node privé et de l'adaptateur compris (229 Mo sur disque, une fois) | progression visible dans le panneau (`real-01-preparation.png`) |
| Relevé du dossier avant / après un tour | 0 à 42 ms sur le bac à sable (5 fichiers) | C3 mesure 45 ms sur 5 011 fichiers |
| Au repos, avant la première ouverture | **0 processus d'agent, 0 WebView du chat**, aucune ligne du chat dans le journal | |
| Au repos, après fermeture | **0 processus d'agent, 0 WebView du chat** | délai réduit à 2,5 s pour l'essai ; 60 s dans le produit |
| Fin de l'essai | plus aucun processus (navigateur, serveur, faux agent) | |

## Accessibilité

Vérifié automatiquement :

- Panneau (arbre d'accessibilité de Chromium, celui que lit un lecteur d'écran) : la carte d'autorisation est une
  boîte d'alerte (`alertdialog`), le fil est un journal vivant (`log`), les 9 boutons présents ont tous un nom.
  Les 179 vérifications de C2 sur la page seule passent aussi (`tests/chat-panel/run.py`).
- Boutons natifs « Demander à l'agent » (bandeau et note) : exposés à UI Automation avec leur nom et leur
  raccourci `Ctrl+Maj+D`, et activés par UI Automation dans le parcours ; contraste du libellé 12,8:1 ; anneau
  de focus clavier dessiné (captures `6.a2`, `6.b2`).
- Clavier : `Ctrl+Maj+D` ouvre et ferme, `Échap` dans le panneau ferme, `Entrée` envoie.

Prévu dans le code mais non essayé : mouvement réduit (pas de glissement, anneau « en cours » fixe), ordre de
tabulation du bandeau (rappel → Demander → Quitter), raccourcis du navigateur tapés depuis le panneau.

Non vérifié : lecture réelle par le Narrateur (carte d'autorisation, texte en flux).

## Suites du dépôt

`python tests/chat/run_suites.py` (une suite après l'autre, journaux dans `build/chat-qa/suites/`) : **27 vertes
sur 31.**

Vertes : barrière de traduction (0 constat), tests du chat (120), copies de fichiers (807), client ACP et
runtime (415 + 5 548), panneau (179), annotations (magasin, pont MCP, courses…), rendu natif des annotations
(1 561, dont les nouveaux boutons), test de fumée `test.ps1`, raccourcis, accessibilité, vue partagée, système
de design, éléments, profils (3), favoris, historique, téléchargements, audio, erreurs de navigation,
mots de passe, import, bulle d'état, glisser d'onglets, favicon, épingle.

Rouges, sans lien avec le chat :

| Suite | Cause | Preuve |
| --- | --- | --- |
| `annotation-settings` | 7 vérifications de connexion Claude / Codex échouent | mêmes 7 échecs sur une copie propre de `main` (a13e032) lancée dans les mêmes conditions : `build/chat-qa/suites/main-baseline-annotation-settings.log` |
| `sandbox` | `error_pages_test` : « DNS: search, never auto retry » | même échec avec les sources de `main` ; fichiers non modifiés sur la branche |
| `ui` | un banc de test ne s'assemble pas (`RemoveWindowSubclass` introuvable : bibliothèque absente de sa ligne de commande) | `chrome.h` et `test-ui.ps1` non modifiés sur la branche ; les 4 bancs précédents de la suite passent |
| `popups` | `popup_shadow_test` : « NOT VERIFIED (no composed desktop) » | le test dit lui-même que le bureau était verrouillé |

Au premier passage, deux suites (`annotations`, `annotation-settings`) avaient échoué par ma faute : mon lanceur
leur passait `BROWSER_ANNOTATIONS_TEST=1`, qui change leur comportement. Lanceur corrigé, `annotations` rejouée :
verte.

`tests/update-integration` (installation et mise à jour) : non lancée. Sa ligne d'assemblage, que j'ai modifiée
pour y ajouter les sources du chat, a été vérifiée seule (elle s'assemble).

## Essai réel (signalé)

**Un seul envoi à Claude Code**, le 18 novembre vers 01 h 20, depuis `python tests/chat/chat_qa_real.py`, dans
la copie du bac à sable (`build/chat-qa-work/site`). Rapport : `build/chat-qa/real-trial.json`, captures
`real-01` à `real-04`.

- Demande : « Dans styles/checkout.css, ajoute « margin-left: auto; » dans la règle .btn-primary pour aligner ce
  bouton à droite. Ne change rien d'autre et ne lance aucune commande. Réponds en une phrase. »
- Claude Code 2.1.326 trouvé, déjà connecté. Node privé et adaptateur téléchargés (empreintes vérifiées par C1).
- La session a démarré en mode « auto » ; le navigateur l'a passée en « default » (Demander) avant le message.
- Une carte « Claude veut modifier checkout.css » (+1 −0) ; rien n'était écrit avant mon accord.
- Réponse : « J'ai ajouté margin-left: auto; dans la règle .btn-primary de styles/checkout.css, ce qui pousse le
  bouton « Payer 48,00 € » à droite du formulaire. » 1 fichier modifié, page rechargée, élément marqué,
  annotation terminée. Claude a aussi donné un titre à la conversation.
- « Tout annuler » a remis le fichier à l'identique.

Un premier lancement du script s'était arrêté avant tout envoi (il prenait « préparation nécessaire » pour un
échec) : aucun message n'était parti (vérifié dans le journal), le script a été corrigé puis relancé une fois.

## Empreintes des vraies configurations (SHA-256)

| Fichier | Avant (17/11, 23:20) | Après (18/11) |
| --- | --- | --- |
| `%APPDATA%\Claude\claude_desktop_config.json` | `0125c5c2e9c61eae…ffc8991` | identique |
| `%USERPROFILE%\.codex\config.toml` | `ef687c8f9096da32…c322a61` | identique |
| `%USERPROFILE%\.claude\settings.json` | `6070a9542dbf2619…cae573ab` | identique |

Valeurs complètes : `build/chat-qa/fingerprints-before.json` et `fingerprints-after.json`. Chaque lanceur de test
les vérifie aussi de son côté.

## Limites connues

- **Captures** : session Windows verrouillée → la barre du haut et les fenêtres natives sont noires dans les
  PNG (`PrintWindow`). Les rendus des deux boutons natifs viennent du banc hors écran.
- **React 19** : pas de fichier:ligne (il faudrait lire les source maps). React < 19 et Astro : oui.
- **Premier tour d'une première session** : la ligne « modèle · effort » du tour est vide, parce que la liste
  des modèles n'arrive qu'au démarrage de l'agent.
- **Modifier un message** : les fichiers reviennent (si demandé) mais la mémoire de l'agent ne revient pas en
  arrière.
- **Conflit à l'annulation** : refus avec les fichiers nommés ; pas encore de choix « garder les miens / écraser ».
- **Mode** : jamais mémorisé ; chaque nouvelle session repart en « Demander ».
- **Détection du dossier** : impossible si le serveur du site tourne sous un autre compte ou en administrateur
  (on passe alors par le sélecteur de dossier).
- **Serveur de développement** : s'il recharge déjà la page, le navigateur ne recharge pas (reconnu par la
  présence de son client dans la page : Vite, webpack, Next, Astro). Seul le serveur simple du bac à sable a
  été essayé.
- **Annulation et annotation** : après « Tout annuler », l'annotation reste « terminée » dans l'historique.

## Non vérifié

- Lecteur d'écran réel (Narrateur).
- Codex en réel ; connexion à un agent depuis le panneau (console de connexion de Claude, navigateur pour Codex).
- Un vrai site Astro, Vite ou Next (rechargement à chaud, attributs réels) ; un vrai site React.
- Agent absent de l'ordinateur, version trop ancienne, panne réseau pendant le téléchargement (testés par C1
  dans ses modules, pas depuis le panneau).
- Fenêtre très étroite, plein écran, vue partagée et changement de profil avec le panneau ouvert.
- Images collées dans la saisie, menu « @ », commandes « / ».
- Suite `tests/update-integration` complète.
