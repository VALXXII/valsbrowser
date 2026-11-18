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
| `src/chat-tools.h` | Quel outil l'agent demande : nom d'outil MCP, nos cinq outils d'annotations (pur, testé) |
| `src/chat-batch.h` | « Traiter les annotations » : ce que devient une ligne de la carte, consigne envoyée à l'agent (pur, testé) |
| `src/chat-host-batch.h` | « Traiter les annotations » côté navigateur : lot posé dans la saisie, tour, suivi de la carte |
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

1. Le dossier vient d'une **détection sûre** (utilisé directement, voir « Dossier du projet sans écran »), sinon
   d'un **choix** de l'utilisateur : « Utiliser » sur le dossier proposé, ou le
   sélecteur de dossier Windows.
2. **Dossier proposé** : le processus qui écoute le port du site (`GetExtendedTcpTable`, sans droits admin)
   → son dossier de travail, lu dans ce processus → remonté jusqu'au premier dossier qui contient
   `package.json`, `.git`, etc. Si Windows refuse la lecture (autre utilisateur, processus élevé), il n'y a
   simplement pas de proposition. Racine de disque, dossier Windows, dossier du profil : jamais proposés.
3. **Sinon** : le sélecteur Windows s'ouvre sur le dossier utilisé le plus récemment pour un autre site.
4. **Mémorisation** : `<données>\annotations-chat\projects.json`, par origine (dossier, qui l'a choisi, agent,
   modèle). Modifiable depuis la puce dossier de la saisie. Rien n'est écrit dans le projet.

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

## Autorisations lisibles (contrat B 1.2)

Le titre d'une carte est une **phrase écrite par le navigateur**, verbe d'abord, jamais le nom technique de
l'outil. Le nom brut et les arguments vont dans « Détails » (`tool {name, server, label?, serverLabel?}` et
`input`, 4 000 caractères au plus).

| Ce que l'agent demande | Titre de la carte |
| --- | --- |
| Modifier, supprimer, lire un fichier | « Claude veut modifier checkout.css » (inchangé) |
| Une commande | « Claude veut lancer une commande » (inchangé) |
| Un outil d'un autre serveur MCP | « Claude veut utiliser l'outil « create_issue » de « github » » |
| `browser_annotations_list` | « Lire les annotations en attente de ce site » |
| `browser_annotation_get` | « Ouvrir l'annotation « Aligne le bouton à droite » » |
| `browser_annotation_claim` | « Prendre en charge l'annotation « … » » |
| `browser_annotation_resolve` | « Marquer l'annotation « … » comme terminée » |
| `browser_annotation_release` | « Rendre l'annotation « … » » |

« … » est la note de l'annotation, sur une ligne, 40 caractères au plus ; si on ne la connaît pas : « une
annotation ». Les agents nomment un outil MCP dans le titre de leur appel : `mcp__<serveur>__<outil>` (Claude
Code, serveur normalisé) ou `mcp.<serveur>.<outil>` (Codex). `src/chat-tools.h` lit les deux formes.

### Nos outils d'annotations passent sans carte (choix assumé)

Les cinq outils ci-dessus ne lisent et n'écrivent **que le magasin d'annotations** du navigateur : jamais le
code du projet, jamais une commande, jamais le réseau. Demander l'accord à chaque lecture d'annotation n'apprend
rien à l'utilisateur et l'habitue à cliquer « Autoriser » sans lire. Donc :

- le navigateur répond lui-même à la demande, par l'option **« une fois »** de l'agent (`allow_once`), **dans
  tous les modes**, « Demander » compris ; aucune carte ne s'affiche ;
- jamais « toujours autoriser » : chaque appel est répondu ici, rien n'est enregistré chez l'agent ;
- le fil garde la trace : une ligne d'étape en clair (« A lu 2 annotations », « A marqué l'annotation « … »
  comme terminée »), et le journal écrit `CHAT annotation tool allowed without a card: <outil>`.

Garde-fous : il faut **le nom exact de notre serveur** (celui donné à la session, `browser-annotations` ou
`browser-annotations-preview`) **et** l'un des cinq noms d'outil exacts. Un serveur au nom voisin, un outil
inconnu sur notre serveur, une demande sans option « une fois » : carte ordinaire. Limite connue : la
reconnaissance se fait sur le nom que l'agent écrit ; un autre serveur MCP que l'utilisateur aurait lui-même
configuré sous exactement ce nom serait traité comme le nôtre.

### Ligne d'étape et résumé replié (contrat B 1.3)

Chaque étape de nos outils part avec son titre en clair et le type `other` (jamais `read` : une annotation
n'est pas un fichier). Une fois finie, elle porte aussi `summary` (`verb`, `object`, et selon le cas `after`,
`afterMany`, `count`, `one`, `many`) : le panneau s'en sert pour la ligne repliée d'un groupe de plusieurs
étapes (« a lu 3 annotations »). Un groupe d'une seule étape affiche son titre tel quel.

## Dossier du projet sans écran (contrat B 1.4)

À l'ouverture du chat, le navigateur cherche où est le code : le processus qui écoute le port du site, et son
dossier de travail. **Si la détection est sûre, le dossier est utilisé tout de suite**, sans l'écran « Où est le
code de ce site ? ». Il s'affiche dans une petite puce en bas de la saisie (icône dossier + nom ; clic = changer).

Sûr = tout cela à la fois (`ChatFolder::Judge`, puis `ChatPrompt::MarksInFolder`) :

- un seul processus sert ce port ;
- le dossier proposé **est** son dossier de travail (pas un dossier parent trouvé en remontant) ;
- ce dossier ressemble à un projet (`package.json`, `astro.config.*`, `vite.config.*`, `next.config.*`,
  `index.html`, `.git`…) ;
- aucun dossier juste au-dessus (3 niveaux) ne porte un fichier de projet : sinon on sert sans doute un morceau
  d'un projet plus grand (`dist\`, `public\`, un paquet parmi plusieurs) et le choix revient à l'utilisateur ;
- ce n'est ni le dossier utilisateur, ni le Bureau, Documents ou Téléchargements eux-mêmes, ni la racine d'un
  disque, ni un dossier système ;
- les repères fichier:ligne de la page, s'il y en a (attributs Astro, sources React), pointent tous dans ce
  dossier. Les dépendances (`node_modules`) ne comptent pas.

Dans tous les autres cas (rien trouvé, plusieurs processus, repères hors du dossier, dossier trop large…),
`state.askFolder` vaut vrai : le panneau montre l'écran de choix, avec le dossier trouvé en suggestion s'il y en
a un. Le journal dit pourquoi (`CHAT folder to confirm: …`).

**Un port resservi par un autre projet.** Le dossier d'un site est retenu, mais la détection est refaite à
chaque ouverture (les ports comme 5173 passent d'un projet à l'autre). Si elle est sûre et désigne un autre
dossier : un dossier que le navigateur avait trouvé seul est remplacé (nouvelle conversation) ; un dossier que
l'utilisateur avait **choisi lui-même** n'est jamais remplacé en silence, l'écran de choix revient. S'il avait
choisi ce dossier à la place de celui que la détection proposait, ce choix est retenu : la question ne revient
pas à chaque ouverture, seulement si le site est un jour servi depuis un troisième dossier. Un changement de
dossier n'est pas accepté pendant un tour.

**Agent.** Même logique : pas d'écran. L'agent est celui de ce site la dernière fois, sinon Claude Code s'il est
installé, sinon le premier agent installé.

## « Traiter les annotations » (contrat B 1.4, Paper 186K-0)

Trois entrées, un seul chemin : la suggestion du chat vide, la commande `/annotations` du panneau, et
`Chat::ProcessAnnotations()` côté navigateur (ouvre le chat puis pose le lot). Ce dernier point d'entrée est
prêt pour un bouton « Traiter avec l'agent » dans l'historique natif ; **ce bouton reste à dessiner**, il n'est
pas ajouté.

1. **Le lot arrive dans la saisie** (`annotations.batch`) : les annotations en attente de ce site lues dans le
   magasin (50 au plus, les plus anciennes d'abord), chacune avec sa note, son élément, son fichier:ligne s'il
   est dans le dossier du projet, et sa miniature. Toutes cochées. Aucune en attente : un bandeau le dit.
2. **Envoi** (`send.annotations`) : seules les lignes cochées partent ; le texte de la saisie est une précision
   facultative. Le message de l'utilisateur porte la carte (`message.user.annotations`) et la **consigne
   exacte** envoyée à l'agent (`ChatBatch::Instruction`, repliée sous « Voir la consigne »). C'est le seul
   endroit où les noms techniques des outils apparaissent.
3. **Suivi** (`annotations.status`) : l'agent traite chaque annotation avec nos outils, sans carte
   d'autorisation (voir plus haut). Chaque appel réussi fait avancer la ligne :

   | Outil appelé par l'agent | Ligne de la carte |
   | --- | --- |
   | `browser_annotation_claim` | « Claude s'en occupe » |
   | `browser_annotation_resolve` (résolue) | Terminée, avec la phrase de résumé de l'agent |
   | `browser_annotation_resolve` (autre issue) ou `browser_annotation_release` | Non traitée, avec sa raison |

   Une ligne ne fait qu'avancer. Le magasin d'annotations dit la même chose par un autre chemin (c'est le
   connecteur qui l'écrit) : il est relu après chaque appel et à la fin du tour, pour rattraper un événement
   manqué.
4. **Fin du tour** : ce qui reste est dit clairement. Une annotation prise mais pas terminée est rendue tout de
   suite (sinon elle resterait « en cours » 30 minutes) ; une annotation jamais commencée reste en attente. Les
   deux apparaissent « non traitées » avec la raison.
5. **Clic sur une ligne** (`annotation.show`) : l'élément est amené à l'écran et encadré un instant.

Les statuts sont enregistrés dans la conversation : une conversation rouverte montre la carte telle qu'elle a
fini. Les annotations décochées, ou non traitées, restent en attente et reviennent au lot suivant.

## Touche Alt seule

Un appui sur Alt seul demandait à Windows la barre de menus de la fenêtre. Le navigateur n'en a pas ; la fenêtre
passait alors en « menu système » et la vue qui avait le clavier (la page ou le panneau du chat) restait figée
jusqu'à la touche suivante. `main.cpp` ignore maintenant cette demande (`WM_SYSCOMMAND`, `SC_KEYMENU` sans
touche). Alt+Espace ouvre toujours le menu système.

## Jauge de contexte (contrat B 1.2)

`usage_update` de l'agent → message `usage {used, size}` au panneau, tel quel (donc aussi après un résumé
automatique de la conversation, quand l'agent renvoie sa nouvelle valeur). La dernière valeur est gardée avec la
conversation (`usedTokens`, `windowTokens`) et renvoyée à chaque `thread.reset` : ouverture du panneau,
conversation rouverte depuis l'historique. Nouvelle conversation ou taille 0 : pas de jauge.

Changement d'agent : le menu envoie `setAgent {id}` ; l'hôte démarre une nouvelle conversation avec cet agent
(l'ancienne reste dans l'historique ; si elle était vide, elle est simplement reprise par le nouvel agent).

## Panneau de note : deux boutons, coins lisses

**Boutons.** Quand le chat est disponible (site local), la rangée finit par « Ajouter » (secondaire, fond
neutre) puis « Demander à l'agent » (principal : à droite, seul bouton rempli de la couleur d'accent). Sans le
chat, « Ajouter » reste seul et principal, comme avant.

| Touche | Avec le chat | Sans le chat |
| --- | --- | --- |
| `Entrée` | Demander à l'agent (action principale) | Ajouter |
| `Ctrl+Entrée` | Ajouter | Ajouter |
| `Ctrl+Maj+D` | Demander à l'agent | — |
| `Maj+Entrée` | Nouvelle ligne | Nouvelle ligne |
| `Tab` | Champ → Ajouter → Demander à l'agent → « Ce que l'agent verra » | Champ → Ajouter → … |

Les lecteurs d'écran annoncent ces raccourcis sur le champ et sur chaque bouton. Pendant l'enregistrement
d'une note, « Demander à l'agent » est atténué et inactif.

**Coins.** Le panneau de note était la seule surface du mode Annotation découpée par une *région de fenêtre* :
un masque à 1 bit, donc des coins en escalier et une bordure rognée. Les autres surfaces (bandeau, lanceur,
carte, historique et son message, modale « Connecter un agent », cadre de page) sont des calques à
transparence par pixel et étaient déjà lisses : vérifié dans le code et par test (aucune région de fenêtre).
Correction à la racine : le panneau est maintenant un calque du même type (`Layer`), sans région et sans
arrondi DWM (qui apporte toujours une ombre, voir `native-ui.h`). Un calque ne peut pas contenir de fenêtre
enfant ; le champ de saisie natif vit donc dans une seconde fenêtre, rectangulaire, posée exactement sur le
champ (13 px à l'intérieur du bord : jamais sous un coin) et rattachée au panneau pour rester juste au-dessus.
Elle porte le focus clavier ; le panneau lui-même n'est jamais activé (comme la pastille « Ce que l'agent
verra »). L'animation d'entrée et de sortie passe toujours par le calque fantôme, y compris en mouvement réduit
(fondu seul).

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
- `annotation-script.h` : `source()` (Astro / React), `sources()` (les fichiers que la page nomme, pour vérifier
  le dossier détecté) et la commande `mark` (étiquette « en cours », coche, cadre « montrer » d'un instant).
- `annotation-note.h` : bouton « Demander à l'agent » (sites locaux, action principale, `Entrée`), `Ctrl+Maj+D`
  dans le champ ; panneau en calque à transparence par pixel (voir « Panneau de note »).
- `annotation-banner.h` : bouton « Demander à l'agent », allumé quand le panneau est ouvert.
- `tools/release/build.py` : sources du chat, faux agent dans `<out>-test`, fichiers de test du panneau exclus.

## Variables d'essai

| Variable | Effet |
| --- | --- |
| `BROWSER_ANNOTATIONS_TEST=1` + `BROWSER_CHAT_FAKE_AGENT=<exe> [options]` | Faux agent à la place des deux agents (jamais dans une build `release`) |
| `BROWSER_CHAT_PANEL_IDLE_MS` | Délai avant destruction du panneau fermé (défaut 60 000) |
| `BROWSER_CHAT_AGENT_IDLE_MS` | Délai avant arrêt de l'agent sans tour (défaut 600 000) |
| `BROWSER_CHAT_DEVTOOLS=1` (mode test seulement) | Outils de développement dans le panneau |
