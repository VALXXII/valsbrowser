# Sandbox — mode Annotation + MCP local (V1)

Une page de paiement « Boutique » pour essayer le mode Annotation du **build preview** et le connecteur MCP, sans rien installer ni toucher à vos navigateurs ou à vos agents.

## Lancer

```
python sandbox/annotations/run-sandbox.py --build
```

`--build` (re)construit `build/annotations-preview` (≈ 2 min) ; sans lui, le build existant est utilisé. Le script :

1. démarre `serve.py` : la page sur `http://127.0.0.1:5173/`, une iframe d'une **autre origine** sur `http://127.0.0.2:5174/` ;
2. lance `build/annotations-preview/browser.exe` (profil, session et magasin propres à ce dossier). `browser.exe` n'accepte pas d'URL en argument : si le preview n'a pas encore de session, le script en écrit une qui ouvre la page ; sinon il vous affiche l'adresse à coller (votre session preview n'est pas modifiée) ;
3. affiche la configuration MCP et les commandes `try-mcp.py` ;
4. quand vous fermez le navigateur (ou Ctrl+C), arrête les serveurs.

Serveurs seuls : `python sandbox/annotations/serve.py` (Ctrl+C pour arrêter).

## Scénario pas à pas

1. **Activer** : bouton « Annoter » de la barre (à droite du champ d'adresse), **Ctrl+Maj+A**, ou clic droit sur la page → « Annoter l'élément ». Le bandeau « Mode Annotation » apparaît sous la barre, le lanceur en bas à droite.
2. **Survoler** : un cadre bleu suit l'élément sous le pointeur avec une étiquette (`button.btn-primary · 240×44`). ↑/↓ : parent/enfant ; Tab suit le focus de la page ; Entrée sélectionne.
3. **Annoter l'élément** : cliquez « Payer 48,00 € » (le clic n'atteint pas la page). Le popover s'ouvre : écrivez la remarque, vérifiez « Ce que l'agent verra » (page, élément, texte, styles, miniature recadrée), puis **Ajouter** (Entrée).
4. **Voir la carte** : sans agent connecté, « Annotation ajoutée · Boutique — Aucun agent ne la lit encore » avec « Connecter un agent » ; avec un agent, « Annotation ajoutée » seule, qui part après 4 s.
5. **Annoter une sélection** : quittez le mode (Échap), sélectionnez « Stylo plume » dans le récapitulatif, clic droit → « Annoter la sélection ».
6. **Autres cibles** : l'iframe « Livraison » (même origine : l'élément intérieur est annoté), l'iframe « PayCo » (autre origine : annotée comme une boîte), le code promo (shadow DOM ouvert : `shadowPath`), le champ sécurisé (shadow DOM fermé : l'hôte seulement), le graphique (canvas : image seulement). « Changer d'articles » remplace la liste, « Re-rendre la page » recrée tous les nœuds (le mode reste actif), « Aller à /merci » change l'URL (le mode se ferme : la page a changé).
7. **Connecter un agent (à la main)** : lanceur ou « Connecter un agent » → choisissez l'app → l'onboarding affiche le bloc exact (chemin réel de `browser-annotations-mcp.exe` à côté de `browser.exe`) avec « Copier ». Collez-le vous-même dans Claude Desktop (Réglages › Développeur › Modifier la config, fusionnez « mcpServers ») ou dans `~/.codex/config.toml`. Rien n'est écrit dans la configuration de vos apps. Étape 3 « Vérifier » : demandez « Liste mes annotations Boutique » à l'agent ; la fenêtre passe à « … a lu vos annotations » dès la première lecture.
8. **Sans agent** : `try-mcp.py` joue le rôle du client MCP sur le magasin du preview :
   ```
   python sandbox/annotations/try-mcp.py list
   python sandbox/annotations/try-mcp.py get <id>          # contexte Markdown + capture → build/annotations-sandbox/<id>.png
   python sandbox/annotations/try-mcp.py claim <id>        # le lanceur affiche le badge, la carte « Prise par try-mcp »
   python sandbox/annotations/try-mcp.py resolve <id> Bouton aligné à droite dans Checkout.tsx
   python sandbox/annotations/try-mcp.py release <id>      # (au lieu de resolve) rend l'annotation
   ```
9. **Voir la résolution** : cliquez le lanceur (ou réactivez le mode) : carte « Résolue par try-mcp (sandbox) » avec le résumé. Si la carte est ouverte pendant la résolution, elle se met à jour toute seule. « Ouvrir » montre le dossier de l'annotation (`annotation.json`, `shot.png`, `state.json`).
10. **Copier en Markdown** : clic droit → « Copier la dernière annotation (Markdown) ».

Le mot de passe pré-rempli (`MDP-SANDBOX-7f3a9c`) et l'e-mail ne doivent jamais apparaître dans le magasin :
`grep -r "MDP-SANDBOX\|client@exemple" build/annotations-preview/annotations` ne doit rien trouver.

## Volontairement non couvert (V1)

- Pas de paquet `.mcpb` : l'option Claude Desktop affiche la configuration manuelle (« extension bientôt »).
- Pas de tiroir « Annotations » (liste, rouvrir, supprimer, brouillons) ; « Garder en brouillon » n'existe pas dans le popover. La carte et l'onboarding sont les seules surfaces de suivi.
- Contenu des iframes d'une autre origine (boîte seulement), racines shadow fermées (hôte seulement), canvas/WebGL interprétés.
- Annotation du chrome natif du navigateur ; push vers un agent ; plusieurs éléments par annotation.
- Builds `--channel release` : ni mode Annotation ni connecteur.
