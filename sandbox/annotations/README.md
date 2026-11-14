# Sandbox — mode Annotation + MCP local (V2)

Une page de paiement « Boutique » pour essayer le mode Annotation du **build preview** (annotations groupées par site, capture prise au clic, connexion des agents en un clic, lanceur et historique) et le connecteur MCP, sans toucher à vos navigateurs ni, par défaut, à vos agents.

## Lancer (une commande)

```
python sandbox/annotations/run-sandbox.py
```

Le script construit `build/annotations-preview` s'il manque (`--build` pour forcer, ≈ 1 min), démarre les deux serveurs locaux (`http://127.0.0.1:5173/` et une iframe d'une **autre origine** sur `http://127.0.0.2:5174/`), ouvre le navigateur preview sur la page, affiche les commandes utiles, puis arrête les serveurs quand vous fermez le navigateur (ou Ctrl+C) : rien ne reste en marche.

Le preview a son propre profil, ses réglages, sa session et son magasin d'annotations, tous dans `build/annotations-preview/`. `browser.exe` n'accepte pas d'URL en argument : si le preview n'a pas encore de session, le script en écrit une qui ouvre la page ; sinon il affiche l'adresse à coller. Si une fenêtre du preview est déjà ouverte, fermez-la d'abord (une seconde instance rejoint la première et n'hérite pas des variables d'environnement du script).

Options : `--agents sandbox|real` (voir ci-dessous), `--reset-launch` (la modale de connexion se rouvrira : compteur des « Plus tard » remis à 0), `--no-browser` (serveurs seuls).

## « Connecter avec Claude / Codex » : ce qui est écrit, et où

Dans le **vrai navigateur**, un clic sur « Connecter avec Claude » ou « Connecter avec Codex » **modifie la vraie configuration de l'app sur votre ordinateur** — uniquement sur ce clic explicite, jamais avant :

- **Claude Desktop** : si le type de fichier `.mcpb` est associé à Claude, le navigateur crée une extension `.mcpb` et l'ouvre (Claude affiche sa fenêtre « Installer ») ; **sinon** (cas de ce poste : `.mcpb` n'a aucune association), il ajoute une entrée `mcpServers.browser-annotations-preview` dans `%APPDATA%\Claude\claude_desktop_config.json`.
- **Codex** : `codex mcp add` si `codex.exe` est dans le PATH, sinon une table `[mcp_servers.browser-annotations-preview]` dans `config.toml` (dossier `CODEX_HOME`, par défaut `%USERPROFILE%\.codex`).

Avant toute écriture, une **sauvegarde horodatée** est faite à côté (`<fichier>.browser-annotations-AAAAMMJJ-HHMMSS.bak`), l'écriture est atomique et ne touche à aucune autre clé (les autres serveurs, réglages, commentaires restent identiques octet pour octet). Un fichier illisible (JSON invalide…) est refusé : rien n'est écrit, la modale affiche l'échec. **« Déconnecter »** (sur la ligne d'une app connectée) retire seulement notre entrée. Une extension installée depuis le `.mcpb` se désinstalle dans Claude › Réglages › Extensions (le navigateur ne modifie jamais les fichiers internes de Claude).

C'est pourquoi la sandbox redirige ces écritures **par défaut** :

| Mode | Commande | Où vont les écritures |
| --- | --- | --- |
| **sandbox** (défaut) | `run-sandbox.py` ou `run-sandbox.py --agents sandbox` | `build/annotations-sandbox/agent-configs/` : un faux `AppData\Roaming\Claude\claude_desktop_config.json` et un faux `home\.codex\config.toml` (chacun avec une entrée « autre-serveur » pour constater qu'elle est conservée). Aucun `.mcpb` n'est ouvert, la CLI `codex` n'est pas lancée. Vos vraies configurations ne sont pas lues pour écrire, ni modifiées. |
| **réel** | `run-sandbox.py --agents real` | Vos vraies configurations, comme décrit plus haut (sauvegarde + « Déconnecter »). À utiliser pour essayer pour de bon avec Claude Desktop ou Codex. |

Mécanisme : le script lance le navigateur avec la variable d'environnement `BROWSER_ANNOTATIONS_CONNECT_ROOT=<dossier>` ; le navigateur y place alors `%APPDATA%`, `%LOCALAPPDATA%`, `%USERPROFILE%` et `CODEX_HOME` pour la connexion des agents (et seulement pour elle), désactive l'ouverture du `.mcpb` et la CLI (`BROWSER_ANNOTATIONS_CONNECT_PATH=<dossier>` permet d'y mettre un `codex.exe` de test). Le journal le dit : `ANNOTATE connect modal opened (…, writes redirected to BROWSER_ANNOTATIONS_CONNECT_ROOT)`.

Réglage manuel (repli, toujours possible) : dans la modale, « Autre app ou réglage manuel » montre la commande exacte (chemin réel de `browser-annotations-mcp.exe` + `--store <magasin>`) avec « Copier », « JSON » et « TOML ».

## Scénario complet

1. **Lancer le mode** : bouton « Annoter » de la barre (à droite du champ d'adresse), **Ctrl+Maj+A**, ou clic droit → « Annoter l'élément ». Le bandeau « Mode Annotation » apparaît sous la barre.
2. **Modale « Connecter un agent »** : aucun agent connecté → elle s'ouvre aussitôt (le mode est déjà actif derrière). Deux chemins :
   - **« Plus tard »** (la croix ou Échap) : on annote tout de suite ; le bandeau garde « Aucun agent connecté · Connecter ». La modale ne revient pas pendant cette session ; elle revient aux lancements suivants, jusqu'à 3 « Plus tard » (compteur dans les réglages du preview), ensuite seul le rappel du bandeau reste. `--reset-launch` remet le compteur à 0.
   - **« Connecter avec Claude » / « Connecter avec Codex »** : le bouton passe « en cours », puis « Claude est connecté » (+ « Redémarrez Claude pour l'activer ») ou un échec expliqué. En mode sandbox, ouvrez `build/annotations-sandbox/agent-configs/…` pour voir l'entrée ajoutée, l'« autre-serveur » intact et le `.bak`. « Déconnecter » retire l'entrée. « Terminé » ferme la modale.
   Le rappel « Connecter » du bandeau rouvre la modale à tout moment (et, plus tard, la section Réglages « Annotations et agents » le permettra aussi).
3. **Annoter** : survolez la page (cadre bleu + étiquette `button.btn-primary · 240×44`, ↑/↓ parent/enfant), cliquez « Payer 48,00 € ». Le clic n'atteint pas la page. Le panneau s'ouvre : site repéré automatiquement (globe + `127.0.0.1:5173`), champ « Que faut-il changer ? », « Ajouter ».
4. **Voir la capture** : elle est prise **au clic** (le cadre bleu est masqué le temps de la prise) ; la pastille « Ce que l'agent verra » sous le panneau la montre (Espace/Entrée ou clic pour la déplier : capture, Page, Élément, Style). Écrivez la note, **Ajouter** (Entrée) : rien n'attend l'image ; si elle n'était pas prête, la note part et l'image la rejoint. Annuler (Échap) jette l'image, rien n'est écrit.
   « Capture impossible » apparaît quand rien de la cible n'est visible au moment de la prise (par exemple une sélection de texte qui a défilé hors de l'écran avant « Annoter la sélection ») ou si le moteur refuse les deux méthodes de capture : la pastille dit « Capture impossible ici · l'annotation partira sans image » et « Ajouter » enregistre la note sans image. (La QA le provoque en faisant défiler la page par script entre le clic droit et le choix du menu.)
5. **Carte** : sans agent, « Annotation ajoutée » + « Connecter un agent » ; avec un agent connecté, « Annotation ajoutée » seule, qui part après 4 s.
6. **Historique** : le lanceur (icône tiroir, en bas à droite de toutes les pages, même hors du mode) ou **Ctrl+Maj+H**. Filtres Toutes / En cours / Terminées, groupé par site (le site de la page d'abord), clic sur une ligne → détail (capture, note, suivi, « Copier en Markdown », « Supprimer »). Le magasin est lu à l'ouverture du panneau et surveillé seulement tant qu'il est ouvert.
7. **Jouer l'agent** avec `try-mcp.py` (client MCP stdio sur le magasin du preview, aucune vraie IA) :
   ```
   python sandbox/annotations/try-mcp.py demo                 # attend la prochaine annotation, la prend, la termine 8 s plus tard
   python sandbox/annotations/try-mcp.py list --site http://127.0.0.1:5173
   python sandbox/annotations/try-mcp.py get <id>             # contexte Markdown + capture → build/annotations-sandbox/<id>.png
   python sandbox/annotations/try-mcp.py claim <id>
   python sandbox/annotations/try-mcp.py resolve <id> Libellé changé dans Checkout.tsx
   python sandbox/annotations/try-mcp.py release <id>         # (au lieu de resolve) rend l'annotation
   python sandbox/annotations/try-mcp.py wait                 # attend une nouvelle annotation ouverte
   ```
   `--site <origine>` lance le connecteur restreint à ce site (`browser-annotations-mcp.exe --site`). Le connecteur n'a pas d'outil d'attente : `wait`/`demo` interrogent la liste toutes les 1,5 s dans une seule session.
8. **Voir « s'en occupe » puis « terminé »** : pendant le `demo` (ou après `claim`), le lanceur montre un anneau et le compteur « 1 », la carte « try-mcp s'en occupe · à l'instant » (« Voir l'annotation » ouvre son détail dans l'historique) ; à la résolution, la carte devient « try-mcp (sandbox) a terminé » avec le résumé, « Recharger la page » et « Rouvrir ». Les cartes suivent les annotations de la session en direct tant que le magasin est surveillé (mode actif, historique ou modale ouverts, ou un agent au travail).
9. **Supprimer + Annuler** : dans le détail (ou le menu « … » d'une ligne), « Supprimer » retire l'annotation tout de suite — `try-mcp.py list` ne la voit plus — et un toast « Annuler » reste 6 s ; « Annuler » (ou Ctrl+Z) la rend intacte (état, capture). Sans « Annuler », elle est effacée pour de bon.
10. **Copier en Markdown** : bouton du détail (contexte complet + chemin de la capture), ou clic droit → « Copier la dernière annotation (Markdown) ».

Le mot de passe pré-rempli (`MDP-SANDBOX-7f3a9c`) et l'e-mail ne doivent jamais apparaître dans le magasin :
`grep -r "MDP-SANDBOX\|client@exemple" build/annotations-preview/annotations` ne doit rien trouver.

## Réglages

- **« Afficher le lanceur d'annotations »** (activé par défaut) et la section « Annotations et agents » (connecter / déconnecter une app, proposer la connexion au lancement, durée de conservation des annotations terminées, dossier, tout effacer) sont livrés par le worker Réglages, en parallèle de cette version de la sandbox. Le mode lit ces réglages s'ils existent dans la build (sinon : lanceur affiché, proposition au lancement active, conservation 30 jours).
- Le lanceur se masque en plein écran (F11 ou vidéo plein écran) et ne prend aucun clic hors de son rectangle.

## Limites connues de la sandbox

- **Installation réelle du `.mcpb` dans Claude Desktop : non vérifiée** (`.mcpb` n'est associé à aucune app sur le poste de test ; le chemin réellement pris y est la fusion de configuration). En mode sandbox, l'ouverture du `.mcpb` est toujours désactivée.
- Contenu des iframes d'une autre origine (boîte seulement), racines shadow fermées (hôte seulement), canvas interprétés : comme en V1.
- Builds `--channel release` : ni mode Annotation, ni lanceur, ni connecteur.
