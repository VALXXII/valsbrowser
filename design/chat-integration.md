# Chat intégré — branchement dans le navigateur (C4)

## En bref

Le chat est un panneau à droite de la page, dans le mode Annotation. On vise un élément, on écrit, l'agent
(Claude Code ou Codex, ceux de l'utilisateur) modifie le code du site, la page se recharge, et on peut tout annuler.

- **Ouvrir** : `Ctrl+Maj+D` (lance aussi le mode Annotation s'il est éteint), le bouton « Demander à
  l'agent » du bandeau bleu, ou (V1.3) **un clic sur un élément de la page** en mode Annotation (site local).
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
| `src/chat-options.h` | Listes modèle / effort telles que le chat les montre : l'entrée « Default » d'un agent, l'effort à poser au démarrage (pur, testé) |
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
   modèle, effort). Modifiable depuis « + › Choisir un autre dossier… » (la puce dossier de la saisie est retirée en V1.2 :
   un seul projet par chat). Rien n'est écrit dans le projet.

Le **mode** (Demander / Auto / Planifier) n'est pas mémorisé : chaque nouvelle session démarre en « Demander ».
L'**effort** (V1.2) : chaque nouvelle session démarre avec l'effort choisi la dernière fois pour ce site, sinon **Moyen** ;
le navigateur le règle juste après le démarrage, comme le mode (`CHAT effort set to « medium »` dans le journal). L'entrée
« Default » des agents n'est jamais montrée ; le modèle qu'elle désigne (Claude : sa description, « Opus 5.5 ») porte le
badge « Par défaut » (sans alias : le modèle que l'agent avait choisi à sa première session).

## Viser depuis le chat

- **V1.3** (décision de l'utilisateur après test, remplace V1.2) : sur un site local, **chaque** clic sur un élément va au
  chat, qui s'ouvre s'il est fermé ; l'élément devient une puce, le curseur de texte va dans la saisie. La boîte de note
  ne s'affiche plus sur un site local. Annotation sans agent : `Ctrl+Entrée` dans la saisie (voir plus bas). Site non
  local, fenêtre privée, ou 10 éléments déjà dans la saisie : la boîte de note, comme avant.
- (V1.2) panneau ouvert, **chaque** clic sur un élément (mode Annotation) va au chat : une puce dans la saisie, et le
  curseur de texte va dans la saisie ; plus de panneau de note. Aussi pendant un tour (la puce attend le message suivant).
  Panneau fermé : le panneau de note, comme avant (annotations sans agent, lues par le MCP). Au-delà de 10 éléments dans
  la saisie, le clic ouvre le panneau de note.
- (V1) Panneau ouvert, viseur allumé : le prochain clic dans la page va au chat (une puce) au lieu du panneau de
  note. C'est le picker et la capture du mode Annotation, sans changement (même image, ≤ 1 600 px).
- (V1, remplacé en V1.2) Pendant un tour, le viseur est éteint : un clic ouvre le panneau de note habituel.
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
code de ce site ? ». (V1.2 : plus de puce dossier dans la saisie ; « + › Choisir un autre dossier… » pour en changer.)

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

## V1.2 — retours du test réel (landing page)

| # | Ce que l'utilisateur a vu | Cause | Correction |
| --- | --- | --- | --- |
| 1 | Le texte de la note n'arrivait pas à l'agent (seule la puce partait) | À la **première** ouverture du chat sur un site, `OpenPanel` démarre une nouvelle conversation, qui vidait `pendingAsk` : le texte posé juste avant était perdu | `pendingAsk` est posé après l'ouverture. Le texte devient le message (bulle et `session/prompt`), avec la puce |
| 2 | « Default » dans l'effort et dans l'en-tête des réponses | L'entrée « Default » de l'adaptateur Claude était montrée telle quelle | Retirée des listes (`src/chat-options.h`) ; **Moyen** posé au démarrage de chaque session, puis le choix de l'utilisateur, mémorisé par site |
| 3 | Puce dossier dans la saisie | — | Retirée (un seul projet par chat) ; la détection et l'écran « Où est le code » restent |
| 4 | L'infobulle de la jauge ne partait pas | Pilotée par `:hover`, que la WebView ne quitte pas quand le pointeur sort tout droit du panneau | Pilotée par le pointeur + `hover` / `pointer.leave` (contrat B 1.5) : l'hôte surveille le vrai pointeur tant qu'une infobulle est ouverte |
| 5 | Le cadre posé sur l'élément décalait au défilement | Le cadre « fait » (coche) était replacé toutes les 400 ms ; le cadre de sélection suivait par images avec un glissé de 120 ms | Dessinés dans des conteneurs ancrés au **document** (le défilement les emporte dans la même image que la page) ; replacés à chaque image pendant un défilement ; plus de glissé tant que l'élément ne change pas |
| 6 | Le curseur disparaissait au-dessus de la page | Le champ de la note (contrôle Windows) masque le pointeur pendant la frappe (« Masquer le pointeur pendant la saisie ») et ne le rend que si la souris repasse sur lui ; au-dessus de la page (une autre fenêtre), il restait caché | Après chaque touche, le navigateur surveille le pointeur ; au premier mouvement, il le rend visible, où qu'il soit |
| 7 | Clic sur un élément, chat ouvert | — | Puce dans la saisie et curseur de texte dans la saisie ; plus de panneau de note quand le chat est ouvert |
| 8 | Nouveau menu modèle · effort (Paper mis à jour) | — | Un groupe par fournisseur connecté, effort en français sans « Default », logo du fournisseur sur le bouton (contrat B §16) |

Le cadre vert « Modifié par Claude » n'existe pas dans le code (voir « Écarts assumés » : la planche 6.11 a été suivie, cadre à la
couleur du profil avec une coche) : le cadre « fait » est celui qui a été corrigé et mesuré.

Parcours en vraie fenêtre (faux agent, listes du vrai adaptateur Claude : `--claude-like`) : `python tests/chat/chat_qa_v12.py` →
`build/chat-qa/v12/` (rapport et captures).

## V1.3 — un clic va toujours au chat, Ctrl+Entrée garde en annotation

- **Clic sur un élément** (mode Annotation, site local) : `Chat::TakeSelection` ouvre le panneau s'il est fermé
  (`CHAT a click on an element opens the chat`), puis la sélection suit le chemin habituel du viseur (capture, fichier:ligne,
  puce). La puce attend que la page du panneau soit prête ; le curseur de texte va dans la saisie dès qu'elle y est.
- **Ctrl+Entrée** dans la saisie (`annotate`, contrat B 1.6) : chaque puce devient une annotation **ouverte** du magasin,
  avec les mots de l'utilisateur (sans le libellé des puces) pour note et la capture de l'élément. Rien n'est envoyé à
  l'agent du chat, rien n'est pris en charge : Claude Desktop, Codex… la lisent par le MCP, et elle compte dans « Traiter
  les annotations ». Le fil reçoit `annotation.kept` (gardé avec la conversation) ; une écriture refusée est dite dans la
  saisie (« Annotation non enregistrée »). Journal : `CHAT annotation kept (Ctrl+Entrée) ann_…: write ok`.
- **Bouton envoyer** : son infobulle dit « Demander à l'agent ↵ » et « Garder en annotation Ctrl ↵ ».
- La boîte de note reste pour les sites non locaux (aucun chat possible) : « Ajouter » seul, `Entrée`.

## V1.4 — interrupteur de la sélection, modes en français

- **Sélection coupée, chat ouvert** (`Ann::SetSelecting`) : le bouton viser de la saisie, `Ctrl+Maj+A` ou le bouton
  « Annoter » de la barre (tant que le chat est ouvert) coupent la sélection : le script de page reçoit `pick false`
  (feuille du curseur vidée, cadre de survol, inspecteur et écrans des iframes retirés, la page reçoit de nouveau ses clics
  et ses touches), le bandeau remonte (la page reprend ses 32 DIP), le bouton « Annoter » s'éteint. Le mode reste actif en
  coulisse : le script reste dans la page pour les cadres du chat (« en cours », coche) et le fichier:ligne. Un nouvel
  appui rallume tout (`pick true`, bandeau). Après un rechargement de la page, l'état coupé est rétabli. Fermer le chat
  pendant que la sélection est coupée quitte le mode. Journal : `ANNOTATE selection off (the chat stays open)` / `on`.
- **Modes** : l'hôte nomme les modes en français (I18n `chat_mode_*`), jamais avec les noms anglais de l'adaptateur :
  l'entrée « ask » = Demander, le premier mode automatique de la table (Claude `acceptEdits`, Codex `workspace-write`) =
  Auto, le plan = Planifier, l'autre mode automatique (Claude `auto`, Codex `agent`) = Autonome ; ordre de Paper 14RK-0.
- Faux agent : `--claude-like` donne aussi les modes de l'adaptateur Claude (Manual, Accept edits, Plan, Auto).

## Mode Commentaire (hôte) — une bulle par élément

Planches Paper 18NS-0 … 18IJ-0, contrat B 1.8 (`design/chat-panel.md` §19, côté page : agent A). Code : `src/chat-comment.h`
(mode), `src/chat-bubble.h` (placement, pur, testé), accroches dans `chat-host*.h`, `annotation-mode.h`,
`annotation-banner.h`, `annotation-script.h`, `annotation-host.h`, `chat-panel-view.h`, `chat-store.h`.

- **Sélecteur du bandeau** (Paper 1AYP-0) : deux icônes à gauche de « Quitter », à la place de « Demander à l'agent » :
  bulle = Commentaire, panneau = Chat à droite. Segment choisi sur blanc 10 % (rayon 6), icône 14 px à 80 % (l'autre à
  50 %). Deux boutons bascule pour les lecteurs d'écran (`AnnotationLayoutComment`, `AnnotationAskAgent`). Le choix est
  mémorisé pour tous les sites (`projects.json` « layout ») ; jamais choisi = Panneau (le comportement d'avant). Ctrl+Maj+D
  ouvre le panneau.
- **La bulle** : une fenêtre popup appartenant à la fenêtre du navigateur, qui héberge **la même WebView** que le
  panneau (déplacée par `put_ParentWindow` ; le panneau la récupère de la même façon) : une seule page, un seul fil.
  305 px CSS de large, la hauteur que la page demande (`bubble.size`), au plus min(520, place libre). Placée dans
  l'espace libre à côté de l'élément (à droite, sinon à gauche, dessous, dessus ; dans la page sinon), jamais hors de la
  zone de page ; le côté choisi est gardé tant qu'il tient (une nouvelle hauteur ne la fait pas sauter). Coins arrondis
  et ombre de DWM. Ordre à l'ouverture : `layout` (bubble, maxWidth, maxHeight, side, element, open), `state`,
  `thread.reset`, `focus`.
- **Suivi de l'élément** : le script de page (`follow`) envoie `anchor` (boîte de l'élément) à chaque image de
  défilement où elle change, toutes les 250 ms sinon (mise en page), et `gone` quand il n'est plus là. L'hôte replace la
  fenêtre aussitôt. Après un rechargement, l'élément est retrouvé par son sélecteur.
- **Fermer** : Échap (`close`), un clic ailleurs (la bulle perd l'activation : page, barre, autre fenêtre), ou
  l'élément qui disparaît. L'agent continue. Un clic sur la page qui vient de fermer la bulle ne commence pas un nouveau
  commentaire ; un clic sur un élément qui a une conversation la rouvre.
- **Une conversation par élément** : l'en-tête de la conversation garde l'élément (`anchor` : sélecteur unique, libellé,
  page sans `#`, fichier:ligne). L'élément de la bulle accompagne chaque message (contrat 1.8 §19.4 : la page ne le liste
  pas ; l'hôte n'envoie pas `element.attached` pour lui). Annotations comme avant : prise en charge, terminée, rendue ;
  Ctrl+Entrée = garder en annotation, avec l'élément.
- **Bordure + icône** (Paper 18IJ-0) : le script de page dessine, pour chaque conversation de la page, un cadre de
  2 px à la couleur d'accent, 4 px autour de l'élément, et une étiquette de 24 px en haut à gauche avec la marque ;
  pendant le travail, un indicateur à la place de la marque ; en attente, « En attente ». Ancrés au document comme les
  autres cadres (collés au défilement). Un clic sur l'élément rouvre sa conversation.
- **Un seul tour à la fois** : pendant que l'agent travaille sur A, la bulle de B peut s'ouvrir et recevoir un message :
  il attend dans la file (`queue.items[].after` = A ; bandeau « Commencera après le travail en cours sur A ») et démarre
  tout seul à la fin du tour de A (nouvelle session pour B). B est alors montrée « à côté » : les messages du fil de A ne
  sont pas envoyés à la page (filtre dans `Send`) ; si A demande une autorisation, un bandeau le dit avec « Voir ».
- **Élément disparu** (pas dans la page actuelle : autre page, ou introuvable après rechargement) : sa conversation est
  listée dans l'historique du lanceur en bas à droite, qui dans ce mode ne liste qu'elles ; un clic la rouvre dans une
  bulle en bas à droite de la page (ou dans le panneau en mode Panneau).
- **Bascule** : rien n'est perdu ; la conversation montrée reste la même (Commentaire → Panneau : le panneau s'ouvre sur
  elle ; Panneau → Commentaire : le panneau se ferme, les cadres apparaissent).

Journal : `CHAT layout: Commentaire…`, `CHAT bubble opened on <élément> for conversation <id>`, `CHAT bubble closed (the
agent goes on)`, `CHAT the next element's turn: conversation <id>`, `CHAT history of the conversations whose element is
gone: <n>`.

Limites connues : pas d'animation native de la fenêtre (l'entrée est jouée par la page, contrat §19.5) ; coins DWM
(rayon de Windows, pas 16 px — corrigé en v2, voir plus bas) ; une conversation ouverte depuis l'historique n'a plus l'image de son élément ;
deux agents en parallèle : non (un tour à la fois, par choix).

## Mode Commentaire v2 (hôte) — retours du premier test réel

Retours R1 à R12 (`plan-annotations-mcp-local/design/chat-comment-v2-retours.md`), valeurs Paper relevées dans
`design/chat-comment-paper-values.md`. Côté hôte : R1, R2, R3 (fenêtre), R4, R6, R8 (consigne), R9, R10, R11 (message).

### R9 — le tour qui restait sur « Edit styles\landing.css » : la cause

Lue dans le journal du test réel (`build/chat-preview/prototype.log`, conversation `c_06c33ac3fbb4`, élément
`div.hero-actions`) :

```
CHAT prompt sent: 1 element(s), 0 image(s)
CHAT bubble closed (the agent goes on)          <- la bulle se ferme juste après l'envoi
CHAT permission asked: edit Edit styles\landing.css
CHAT bubble opened on div.hero-actions …        <- rouverte… puis refermée, rouverte, refermée (3 fois en 2 min)
CHAT mode asked: « acceptEdits »                <- l'utilisateur change de mode : la demande déjà posée ne change pas
                                                    (jamais de « permission answered » : le tour attend toujours)
```

**Cause** : la demande d'autorisation était bien arrivée et bien routée vers la bonne conversation, mais elle n'est pas
un élément du fil. Chaque ouverture de la bulle envoie `thread.reset` (le fil redessiné) — ce qui efface la carte
d'autorisation de la page — et l'hôte ne renvoyait les cartes en attente (`waitingPermissions`) qu'à la création de la
WebView (`ready`). La bulle rouverte montrait donc seulement l'étape de l'outil (« Edit styles\landing.css ») et rien
pour répondre : l'agent attendait sa réponse indéfiniment. En panneau, le panneau reste ouvert pendant le tour : la
carte n'est jamais effacée, d'où « en mode chat c'était rapide ». Ni la session (une par conversation, réutilisée), ni
l'image (8 Ko) n'y sont pour quelque chose.

**Correction** : `SendThread()` renvoie, après son `thread.reset`, chaque `permission.request` encore en attente quand
la conversation montrée est celle du moteur (`src/chat-host.h`) ; le renvoi de `ready` passe par là. En plus, la
fermeture « l'activation est partie ailleurs » ne se fait plus quand le premier plan est vide un instant (un processus
qui démarre) : regardé à nouveau 250 ms plus tard, et une fenêtre de ce navigateur garde la bulle ; le journal dit qui
a pris le premier plan (`the bubble lost the foreground to another application (<classe>, pid <n>)`).

Vérification : `tests/chat/chat_qa_comment_v2.py` (vraie fenêtre, faux agent) — demande, autorisation, bulle fermée
puis rouverte, carte toujours là, « Autoriser » par un vrai clic transmis à la bulle, fin du tour ; puis la même
demande en bulle et en panneau (mesures ci-dessous).

### Mesures R9 (faux agent, même demande, autorisation donnée dès qu'elle s'affiche)

<!-- R9-MEASURES -->

### R1 — vrais coins, sans coin noir : la page dans une fenêtre composée

- La WebView du chat est désormais hébergée **en visuel** (`CreateCoreWebView2CompositionController`,
  `src/chat-panel-view.h`) : la page est un visuel DirectComposition au fond **transparent**
  (`DefaultBackgroundColor` alpha 0). Toujours une seule WebView, une seule page.
- La fenêtre de la bulle est `WS_EX_NOREDIRECTIONBITMAP` (ce que la page ne peint pas est transparent),
  `DWMWCP_DONOTROUND` et sans bordure DWM : plus de coins de Windows (8 px, avec une ombre), plus de région
  (crénelée). La surface (`#171717`, rayon 16, padding 12) est dessinée par la page avec ses coins lissés.
- **Ombre** : Paper n'en dessine aucune sur la bulle (`18TO-0`, `1B60-0` et les 8 autres : ni `boxShadow` ni bordure).
  La bulle n'a donc pas d'ombre, et la fenêtre fait exactement la surface au repos (`inset` 0).
- Le panneau : un enfant `BrowserChatPanelHost` de la fenêtre du navigateur, peint `#171717` sous la page (pas de flash
  blanc au chargement), cible de composition de la page ; il glisse comme avant.
- L'hôte transmet la souris (`SendMouseInput` : boutons, molette, sortie, capture), le curseur (`CursorChanged`) et UI
  Automation (`WM_GETOBJECT` → fournisseur de la WebView) ; le clavier est celui de WebView2 une fois le focus donné
  (un clic dans la page le donne). Glisser-déposer : non (la page le refuse déjà).
- Sans `dcomp.dll` ou `ICoreWebView2Environment3` : hébergement fenêtré comme avant (coins carrés, aucun débordement).
  Journal : `CHAT panel WebView2: visual hosting (transparent)`.

### R3 — menus et infobulles hors de la bulle (contrat B §20)

- La page demande de la place autour de sa surface tant qu'un menu ou une infobulle en sort : `bubble.size` reçoit
  `overflowTop`, `overflowRight`, `overflowBottom`, `overflowLeft` (px CSS, 0 à 400).
- L'hôte agrandit la fenêtre transparente d'autant, **la surface reste à sa place à l'écran**, dans la limite de l'écran
  du navigateur (zone de travail du moniteur, pas seulement la page : un menu peut passer sur la barre d'adresse comme
  dans Paper). Il répond par `layout { mode: "bubble", open: false, inset: {top, right, bottom, left} }` = ce qu'il a
  accordé (moins au bord de l'écran : la page retourne alors son menu). `inset` part aussi avec chaque `layout`
  d'ouverture (0 au repos).
- Pas de réponse à un `bubble.size` identique au précédent (taille et débordements) : pas de boucle.
- Clics : tant que la fenêtre est agrandie, elle prend les clics ; un clic dans sa partie transparente ferme le menu
  (la page s'en charge, comme un menu : ce clic n'agit pas dessous). Un clic hors de la fenêtre ferme la bulle.
- Calcul pur et testé : `ChatBubble::Grow`, `ChatBubble::Bounded` (`src/chat-bubble.h`).

### R2, R6 — bordure + icône, réouverture

- **R2** : un élément n'a sa bordure + son icône qu'après l'envoi du premier message (ou un message en file, ou une
  annotation gardée avec Ctrl+Entrée) : `ChatBubble::Sent`. Clic puis Échap sans rien envoyer : rien ne reste.
- **R6** (cause) : la conversation d'un élément cliqué puis abandonnée (jamais enregistrée) restait dans la liste des
  conversations ancrées ; plus tard, un clic sur l'élément cherchait à la relire, « conversation not found », et la
  bulle ne s'ouvrait pas (19 « conversation not found » dans le journal du test). Maintenant : seules les conversations envoyées sont
  ancrées (R2), et une conversation illisible est oubliée et l'élément s'ouvre sur une nouvelle conversation
  (`ChatBubble::ReopenOf`).

### R4 — exactement les modes de Paper `14RK-0`

`state.modes`, dans cet ordre, textes à la lettre : « Accept edits » / « Automatically accept all file edits » (kind
`edits` : Claude `acceptEdits`, Codex `workspace-write`), « Auto » / « Claude handles permission decisions » (kind
`auto` : Claude `auto`, Codex `agent`, nom de l'agent), « Demander » (kind `ask`), « Planifier » (kind `plan`). Le mode
qui contourne toutes les autorisations n'est jamais proposé, ni aucun autre mode de l'adaptateur ; une ligne que
l'adaptateur n'a pas est absente (`ChatOptions::PaperModes`). Journal : `CHAT modes offered: …`.

### R8, R10, R11, placement

- **R8** : le préambule (version 2, renvoyé aux sessions reprises) demande à l'agent d'écrire d'abord une phrase courte
  qui dit ce qu'il va faire, puis de travailler (`src/chat-prompt.h`).
- **R10** : le masque de Paper `1AYJ-0` est peint sur la trame, le dégradé et la bordure du bandeau, sous son contenu :
  `#171717` à opacité 0,85, alpha de 0 à 60,66 % de la largeur jusqu'à plein à 82,29 % (`BannerSpec::MaskAlpha`, aussi
  dans le tracé partiel de la vague `RasterBandSpan`).
- **R11** : `conversation.rename { id, title }` (page → hôte) : titre sur une ligne, 1 à 120 caractères
  (`ChatPrompt::RenameTitle`), enregistré ; l'hôte renvoie `state` (et `history` quand le renommage vient de la liste).
- **Placement** (Paper, 9 planches sur 10) : bulle à 12 px du bord droit de l'élément et 4 px au-dessus de son haut
  (le haut du cadre de sélection) ; même règle à gauche, dessous et dessus (`ChatBubble::Place`, `lead`).

Limites connues (v2) : le passage panneau ↔ bulle déplace le visuel de la page d'une cible de composition à l'autre
(une image possible avant la nouvelle mise en page) ; pendant l'agrandissement pour un menu vers le haut ou la gauche,
la page doit recaler sa surface sur le nouvel `inset` (un décalage d'une image est possible, non mesuré) ; le
basculement d'un menu au bord de l'écran est fait par la page.
