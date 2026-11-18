# Bac à sable du chat intégré

Un petit site local (« Boutique — Paiement ») pour essayer le chat avec un agent, sans risque : l'agent ne
modifie qu'une **copie** du site, jamais les fichiers du dépôt ni vos projets.

## Lancer

Une seule commande, depuis la racine du dépôt :

```
python sandbox/chat/run-sandbox.py --fake-agent
```

- `--fake-agent` : un **faux agent**. Aucun compte, aucun réseau, aucun coût. Parfait pour voir l'interface.
- sans `--fake-agent` : **vos vrais agents** (Claude Code, Codex), avec votre compte. Chaque message est une
  vraie demande. Au premier message, le navigateur télécharge une fois ce qu'il lui faut (Node privé et
  adaptateur) dans `build/chat-preview/agents` : rien n'est installé ailleurs sur l'ordinateur.
- `--reset` : remet la copie du site à neuf.
- `--build` : reconstruit le navigateur d'essai avant de lancer.

Le navigateur d'essai est `build/chat-preview/browser.exe`. Il a son propre profil : vos onglets et réglages
habituels ne sont pas utilisés. Fermez-le (ou Ctrl+C dans le terminal) : tout s'arrête, rien ne reste en marche.

## Essayer, pas à pas

1. **Ouvrir le chat** : `Ctrl+Maj+D`. (Ou `Ctrl+Maj+A` pour le mode Annotation, puis « Demander à l'agent »
   dans le bandeau bleu.) Le panneau arrive à droite ; la page se resserre, elle n'est pas recouverte.
2. **Le dossier du code** est trouvé tout seul : il s'affiche dans une petite puce en bas de la saisie
   (« site »). Cliquez la puce pour en changer. Le navigateur ne pose la question « Où est le code de ce
   site ? » que s'il n'est pas sûr.
3. **Viser** : cliquez le bouton « Payer 48,00 € » dans la page. Il devient une puce dans la zone de saisie,
   avec son fichier et sa ligne (`index.html:25`).
4. **Demander** : écrivez « Aligne-le à droite », puis Entrée.
5. **Autoriser** : l'agent montre ce qu'il veut changer. « Autoriser » ou « Refuser ».
6. **Résultat** : la page se recharge seule, le bouton est à droite, encadré avec une coche.
   La barre « 1 fichier modifié » permet de **voir les changements** et de **tout annuler**.

Autres choses à essayer :

- Dans le mode Annotation, cliquez un élément de la page : le chat s'ouvre (s'il était fermé), l'élément est dans
  la saisie. `Entrée` demande à l'agent ; **`Ctrl+Entrée` garde une simple annotation**, sans rien envoyer à l'agent
  (une ligne « Gardé en annotation » le confirme). Survolez le bouton d'envoi : il rappelle les deux.
- **Traiter les annotations** : gardez deux ou trois éléments en annotation (`Ctrl+Entrée`), puis dans le chat tapez
  `/annotations` (ou choisissez la suggestion « Traiter les annotations en attente » d'un chat vide). Les
  annotations arrivent cochées dans la saisie ; décochez celles à garder pour plus tard, envoyez. Le message
  devient une carte qui suit chaque annotation : en attente, « Claude s'en occupe », terminée avec son résumé.
- Le petit anneau à côté du titre du chat : la part de mémoire de la conversation déjà utilisée (survolez-le).
- Le bouton « Modifier le panier » simule un site React : sa puce indique `CartButton.jsx:6`.
- Le texte gris « Paiement de démonstration… » n'a aucune information de code : sa puce n'affiche pas de
  fichier (on n'invente jamais).
- L'icône horloge de l'en-tête : les conversations précédentes de ce site.
- Ouvrez un site Internet (par exemple `https://example.com`) et `Ctrl+Maj+D` : le chat s'ouvre désactivé et
  explique pourquoi (il ne fonctionne que sur vos sites locaux).

## Avec le faux agent

Un message ordinaire : il demande votre accord, puis met **vraiment** le bouton « Payer » à droite
(`styles/checkout.css` de la copie). Quelques mots déclenchent d'autres comportements :

| Mot dans le message | Ce que fait le faux agent |
| --- | --- |
| `think` | montre une réflexion avant de répondre |
| `plan` | propose un plan (essayez en mode « Planifier ») |
| `slow` | écrit lentement, sans fin : pour essayer **Arrêter** et la file d'attente |
| `question` | pose une question à choix |
| `refusal`, `crash` | refus de l'agent, arrêt brutal de l'agent |
| `echo` | répète votre message |

Pour « Traiter les annotations », le faux agent fait semblant de traiter chaque annotation (rien n'est modifié
dans le code). Une note qui contient `impossible` est rendue avec une raison ; une note qui contient `oublie`
n'est pas touchée, pour voir ce que la carte en dit.

## Où sont les choses

| Quoi | Où |
| --- | --- |
| Site d'origine (jamais modifié) | `sandbox/chat/site/` |
| Copie que l'agent modifie | `build/chat-sandbox/site/` |
| Navigateur d'essai et ses données | `build/chat-preview/` |
| Faux agent | `build/chat-preview-test/fake-acp-agent.exe` |
| Journal du navigateur | `build/chat-preview/prototype.log` (lignes `CHAT …`) |

`serve.py` sert la copie sur `http://127.0.0.1:5183/` et ajoute à la volée, comme Astro en développement,
le fichier et la ligne de chaque balise (`data-astro-source-file`, `data-astro-source-loc`).

## Sécurité de l'essai

- Vos vraies configurations de Claude et de Codex ne sont jamais modifiées : « Connecter avec Claude / Codex »
  (annotations) écrit dans un dossier de test, `build/chat-sandbox/agent-configs`.
- Avec vos vrais agents, l'agent travaille dans la copie du site seulement, et en mode « Demander » : chaque
  modification passe par une carte d'autorisation.

## La mini landing page « Atelier Papier »

Le bac à sable s'ouvre maintenant sur `landing.html`, une petite page d'accueil (le paiement reste sur `index.html`, lien « Panier »). Quelques défauts y sont laissés exprès pour essayer des demandes :

- le texte sous le grand titre est trop pâle : « rends ce texte plus lisible » ;
- les deux boutons du haut sont collés au texte et entre eux : « espace un peu ces boutons » ;
- la troisième carte « Fait pour durer » casse la grille : « aligne les trois cartes sur une ligne » ;
- le badge « Le plus choisi » dépasse de sa carte : « garde le badge dans la carte » ;
- et tout ce que tu veux : couleurs, textes, mise en page mobile, ajout d'une section…

`--reset` remet la page à neuf (les modifications de l'agent sont faites sur une copie, jamais sur ces fichiers).
