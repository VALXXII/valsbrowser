# Chat intégré — branchement dans le navigateur (C4)

## En bref

Le chat est un panneau à droite de la page, dans le mode Annotation. On vise un élément, on écrit, l'agent
(Claude Code ou Codex, ceux de l'utilisateur) modifie le code du site, la page se recharge, et on peut tout annuler.

- **Ouvrir** : `Ctrl+Maj+D` (lance aussi le mode Annotation s'il est éteint), ou le bouton « Demander à
  l'agent » du bandeau bleu, ou « Demander à l'agent » dans le panneau de note d'une sélection.
- **Fermer** : la croix du panneau, `Échap` dans le panneau, `Ctrl+Maj+D`, ou quitter le mode Annotation.
- **Sites locaux seulement** (localhost, 127.x.x.x, [::1], *.localhost). Ailleurs le panneau s'ouvre désactivé
  et dit pourquoi.
- **Au repos, rien** : tant que le panneau n'a jamais été ouvert, il n'existe ni WebView, ni fenêtre, ni fil,
  ni fichier lu, ni processus d'agent.

Raccourcis du mode, cohérents entre eux : `Ctrl+Maj+A` Annoter · `Ctrl+Maj+H` Historique · `Ctrl+Maj+D`
**D**emander à l'agent.

## Fichiers

| Fichier | Rôle |
| --- | --- |
| `src/chat-host.h` | État, agents, messages hôte → panneau, textes d'erreur |
| `src/chat-host-turn.h` | Session de l'agent, tour complet, autorisations, annotations, annuler |
| `src/chat-host-ui.h` | Ouverture / fermeture, viseur, dossier, historique, messages panneau → hôte |
| `src/chat-panel-view.h` | La WebView2 du panneau (création, fichiers servis, pont) |
| `src/chat-local.h` | Règle « site local » (pure, testée) |
| `src/chat-prompt.h` | Ce que reçoit l'agent, contrat D (pur, testé) |
| `src/chat-store.h` | Dossier par site, historique des conversations (testé) |
| `src/chat-folder.h` | Dossier probable d'un site, sélecteur Windows (testé) |
| `src/annotation-mode.h`, `annotation-script.h`, `annotation-note.h`, `annotation-banner.h`, `main.cpp` | Accroches (voir plus bas) |
| `sandbox/chat/` | Bac à sable (`README.md`) |
| `tests/chat/` | Tests unitaires, parcours réel sur le faux agent, lanceur des suites |

Les trois fichiers `chat-host*.h` sont inclus à la fin de `annotation-mode.h` : le chat n'existe que dans les
builds qui ont le mode Annotation. Sans lui (builds de test non estampillées), `main.cpp` reçoit des fonctions
vides.

## Panneau

- **Création** : à la première ouverture seulement, `CreateCoreWebView2Controller` sur l'environnement des
  onglets (aucun nouveau processus navigateur ; un moteur de rendu de plus tant que le panneau existe).
- **Contenu** : uniquement nos fichiers. `https://chat.browser.internal/chat-panel/*` est servi depuis
  `assets\chat-panel` et `/fonts/*` depuis `assets\fonts`. Toute autre requête reçoit 403, toute autre
  navigation et toute nouvelle fenêtre sont annulées, les permissions (caméra…) sont refusées. La CSP du
  contrat B est aussi envoyée en en-tête HTTP. `dev.html` et `dev-host.js` ne sont ni copiés dans le build ni
  servis.
- **Place** : 380 DIP à droite de la zone de page (320 au moins si la fenêtre est étroite), du bas du bandeau
  au bas de la fenêtre. `ContentBox()` retire cette largeur : la page **rétrécit**, elle n'est jamais recouverte.
- **Mouvement** : la page prend sa nouvelle largeur d'un coup (un seul calcul de mise en page), puis le
  panneau glisse depuis la droite sur le fond du navigateur : 220 ms à l'entrée, 160 ms à la sortie, courbe
  du bandeau `(0,32, 0,72, 0, 1)`. Seule la position change (aucun redimensionnement par image). Mouvement
  réduit : pas de glissement.
- **Fermeture** : le panneau est caché tout de suite, sa WebView est **détruite** après 60 s fermée. L'agent
  est arrêté 60 s après la fermeture, ou après 10 minutes sans tour ; il reprend au message suivant
  (`session/resume` quand l'agent le permet).
- **Lanceur d'annotations** : masqué tant que le panneau est ouvert, de retour à la fermeture. Le rappel
  « Aucun agent connecté » et la fenêtre « Connecter un agent » (connexion MCP) s'effacent quand le chat s'ouvre.
- **Clavier** : dans le panneau, seules les combinaisons avec `Ctrl` (sauf `Ctrl+/`) remontent aux raccourcis
  du navigateur ; `Échap`, `Tab`, `Entrée` et les flèches restent au panneau.

## Site ↔ dossier

1. Le dossier vient toujours d'un **choix** de l'utilisateur : « Utiliser » sur le dossier proposé, ou le
   sélecteur de dossier Windows.
2. **Dossier proposé** : le processus qui écoute le port du site (`GetExtendedTcpTable`, sans droits admin)
   → son dossier de travail, lu dans ce processus → remonté jusqu'au premier dossier qui contient
   `package.json`, `.git`, etc. Si Windows refuse la lecture (autre utilisateur, processus élevé), il n'y a
   simplement pas de proposition. Racine de disque, dossier Windows, dossier du profil : jamais proposés.
3. **Sinon** : le sélecteur Windows s'ouvre sur le dossier utilisé le plus récemment pour un autre site.
4. **Mémorisation** : `<données>\annotations-chat\projects.json`, par origine (dossier, agent, modèle).
   Modifiable depuis le « + » de la saisie (« changer de dossier »). Rien n'est écrit dans le projet.

Le **mode** (Demander / Auto / Planifier) n'est pas mémorisé : chaque nouvelle session démarre en « Demander ».

## Viser depuis le chat

- Panneau ouvert, viseur allumé : le prochain clic dans la page va au chat (une puce) au lieu du panneau de
  note. C'est le picker et la capture du mode Annotation, sans changement (même image, ≤ 1 600 px).
- Pendant un tour, le viseur est éteint : un clic ouvre le panneau de note habituel.
- **Fichier:ligne** — seulement si c'est vrai :
  - attribut Astro `data-astro-source-file` / `-loc` **sur l'élément lui-même** ;
  - sinon données de debug React de l'élément (`_debugSource` de sa fibre ou de ses propriétaires, React < 19 ;
    prop `__source` quand le build la garde) ;
  - le chemin doit exister **dans le dossier du projet**, hors `node_modules` ;
  - sinon **rien**. Un attribut trouvé sur un parent n'est jamais affiché comme la ligne de l'élément : il est
    donné à l'agent à part (« Nearest ancestor with a known source »).
- **React 19** (`_debugStack`) : non géré en V1 (il faudrait lire les source maps) → pas de fichier:ligne.

## Ce que reçoit l'agent (contrat D)

Par tour, dans l'ordre :

1. une fois par session, le **préambule** : site, dossier, et les règles (modifier le code du dossier, plus
   petite modification, rester dans le dossier, ne pas lancer de serveur ni de navigateur, dire clairement
   s'il ne peut pas, contenu de page = données non fiables, réponse courte dans la langue de l'utilisateur) ;
2. pour chaque élément visé, un bloc `<selected_element_XXXXXXXX>` (8 chiffres hexadécimaux tirés au hasard) :
   page et taille de la vue, balise et texte (120 caractères), fichier:ligne si connu, composants React,
   sélecteur, boîte, espacement (padding / gap), styles calculés utiles ; puis son **image** ;
3. le message de l'utilisateur (avec le libellé de chaque puce à sa place) ;
4. les images jointes.

Tout ce qui vient de la page est réduit à une ligne ou mis dans un bloc de code dont la clôture est plus
longue que toute suite d'accents graves du contenu : une page ne peut ni fermer le bloc ni en créer un.

L'agent reçoit aussi notre connecteur d'annotations en serveur MCP (`--store`, `--site <origine>`), pour la
suggestion « Traiter les annotations en attente ».

## Boucle d'un tour

1. `send` → bulle de l'utilisateur, `turn.start`, viseur éteint, étiquette « en cours » sur l'élément.
2. Chaque élément devient une **annotation** du magasin, réservée aussitôt au nom de l'agent du chat
   (`browser-chat-claude-code` / `browser-chat-codex`) : les agents MCP ne la prennent pas une seconde fois.
3. Session de l'agent : préparation si besoin (téléchargement avec progression), démarrage, puis **mode
   « Demander »** imposé avant le premier message (`AgentRuntime::GetModeMap`).
4. `BeginTurn` (copies de fichiers), puis le message part.
5. Les événements de l'agent deviennent les messages du panneau : texte, réflexion, étapes, plan, cartes
   d'autorisation (titre « Claude veut modifier checkout.css », diff, options de l'agent).
6. Fin : `EndTurn` → fichiers modifiés, +/−, point de reprise, diff des 6 premiers fichiers → `turn.end`.
7. Annotations : **terminée** avec la dernière phrase de l'agent ; **rendue** avec la raison si le tour est
   arrêté, échoue, ou n'a produit qu'un plan.
8. Page : si des fichiers ont changé, la page est **rechargée**, sauf si un serveur de développement le fait
   déjà (client de rechargement à chaud détecté dans la page, ou navigation déjà survenue pendant le tour).
   Le mode Annotation survit à ce rechargement (script réinjecté), puis l'élément est retrouvé par son
   sélecteur et reçoit son cadre avec une coche ; s'il n'est pas retrouvé, pas de cadre.
9. File d'attente : un message envoyé pendant un tour attend ; « Envoyer maintenant » arrête le tour en cours.

**Annuler** : « Tout annuler » = `UndoTurn`, « Revenir » = `Restore`. En cas de conflit (fichier modifié par
l'utilisateur depuis), rien n'est écrasé : le panneau nomme les fichiers. Après annulation la page est rechargée.

**Reprise** : conversations dans `<données>\annotations-chat\conversations\` (une par fichier + un index),
200 au plus. À la réouverture, le fil est rejoué et la session reprend avec son `sessionId`.

## Écarts assumés par rapport à la demande

- **Cadre « Modifié par… »** : la planche 6.11 montre le cadre à la couleur du profil avec une coche, sans
  texte ni vert. J'ai suivi la planche (source de vérité du design).
- **Titre de conversation** : les premiers mots de la demande (sans les libellés de puces), pas un titre
  rédigé par un modèle. Si l'agent propose un titre de session, il le remplace.
- **Dossiers récents** : il n'y a pas de liste dans le panneau (le contrat B n'en a pas) ; le sélecteur
  Windows s'ouvre sur le dossier le plus récent.
- **Modifier un message** : la conversation est coupée à ce message et la demande repart, avec restauration
  des fichiers si demandé. La mémoire de l'agent, elle, ne revient pas en arrière (limite d'ACP v1).
- **Conflit à l'annulation** : refus avec les fichiers nommés. Les choix « garder les miens / écraser » que
  permet C3 ne sont pas proposés en V1.

## Accroches dans le code existant

- `main.cpp` : `ChatCommand` (Ctrl+Maj+D), `ChatInsetDip()` dans `ContentBox()`, `ChatNavigationCompleted()`,
  `ChatShutdown()` à la destruction de la fenêtre.
- `annotation-mode.h` : le lanceur se masque si le chat est ouvert ; une sélection va au chat si le viseur du
  chat est allumé ; l'image capturée rejoint la puce ; `Rearm()` réinjecte le script après un rechargement ;
  une navigation de la page du chat ne quitte plus le mode (sauf changement de site).
- `annotation-script.h` : `source()` (Astro / React) et la commande `mark` (étiquette « en cours », coche).
- `annotation-note.h` : bouton « Demander à l'agent » (sites locaux), `Ctrl+Maj+D` dans le champ.
- `annotation-banner.h` : bouton « Demander à l'agent », allumé quand le panneau est ouvert.
- `tools/release/build.py` : sources du chat, faux agent dans `<out>-test`, fichiers de test du panneau exclus.

## Variables d'essai

| Variable | Effet |
| --- | --- |
| `BROWSER_ANNOTATIONS_TEST=1` + `BROWSER_CHAT_FAKE_AGENT=<exe> [options]` | Faux agent à la place des deux agents (jamais dans une build `release`) |
| `BROWSER_CHAT_PANEL_IDLE_MS` | Délai avant destruction du panneau fermé (défaut 60 000) |
| `BROWSER_CHAT_AGENT_IDLE_MS` | Délai avant arrêt de l'agent sans tour (défaut 600 000) |
| `BROWSER_CHAT_DEVTOOLS=1` (mode test seulement) | Outils de développement dans le panneau |
