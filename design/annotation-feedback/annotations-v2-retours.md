# Annotations V2 — retours du premier test réel (14 novembre 2026)

Notés pendant le test du preview 0.3.250 (c62426c) avec les vrais agents. **À lancer seulement au go de l'utilisateur.**

1. **Redémarrer Claude après la connexion.** Après « Connecter avec Claude » (fusion de `claude_desktop_config.json`), Claude Desktop ne voit l'outil qu'après redémarrage. L'état « Connecté » de la modale et la ligne des Réglages doivent le dire clairement (« Redémarrez Claude pour l'activer »), comme pour Codex.
2. **Flash à la capture.** Au clic sur un élément, le cadre de sélection disparaît le temps de la capture : un flash visible, très « clunky ». Piste : ne plus masquer le cadre dans la page. Capturer sans lui (overlay hors du DOM capturé, ou recadrer et effacer le cadre sur l'image), ou garder le cadre visible et le retirer de la capture côté encodage.
3. **Petits flashs ailleurs.** Plusieurs flashs au clic sur les éléments et dans notre UI native en mode annotation (fenêtres qui se recréent ou se repeignent, fond non initialisé, fondus qui partent de 0). À traquer un par un : premier rendu des fenêtres en couches, `WS_EX_NOREDIRECTIONBITMAP` / fond, ordre Show / paint.
4. **Curseur dédié.** En mode annotation, le curseur texte (I-beam) sur du texte n'a pas de sens. Il faut un curseur spécifique au mode (viseur ou pointeur « annoter »), forcé sur toute la page tant que le mode est actif (CSS `cursor` injecté sur `*` dans le script de page, ou curseur natif), en gardant le curseur normal sur notre UI.
5. **L'historique ne montre pas que Claude s'en occupe ou a terminé.** Constat : Claude a lu l'annotation (`readers/claude-ai.json`, `readers/local-agent-mode-…json`), mais n'a jamais appelé `browser_annotation_claim` / `resolve`. Pas de fichier de prise en charge, et `state.json` est inchangé depuis la création. Pistes :
   - instructions MCP du serveur et descriptions d'outils plus directives (« avant de modifier : claim ; après : resolve avec un résumé ; si tu ne peux pas modifier : release avec la raison ») ;
   - un prompt MCP « Traiter mes annotations » ;
   - côté UI, afficher « Lue par Claude · il y a … » (déjà connu par `readers`) même sans claim, pour que l'historique ne paraisse pas mort.
6. **Lanceur visible seulement en mode annotation.** Hors du mode, le bouton historique en bas à droite ne doit pas être là. **Change le contrat V2 §5** (« lanceur toujours disponible »). L'historique reste accessible hors mode par Ctrl+Maj+H (et éventuellement le menu / les Réglages). Le réglage « Afficher le lanceur d'annotations » est à revoir : le supprimer, ou le limiter au mode.
7. **(Proposé, à confirmer)** Connexion en un clic pour **Claude Code** (`claude mcp add`), l'agent « dans le repo » côté Claude. Il est nécessaire pour que Claude modifie vraiment le code. Claude Desktop en chat simple n'a pas accès aux sources.

8. **Paddings et gaps au survol.** Quand le curseur est sur un élément (une « frame »), montrer ses marges internes (padding) et les espacements entre enfants (gap), comme un inspecteur de design. **Design dans Paper d'abord.**
9. **Entrée du mode animée.** Aujourd'hui l'entrée est instantanée. Le bandeau doit arriver en glissant, et tout le viewport reçoit un petit dégradé bleu qui « pop » pendant ~500 ms pour marquer l'entrée.
10. **Bandeau et dégradé vivants.** Animer le bandeau et le dégradé bleu avec une petite vague tramée (dither) animée.

11. **Aperçu « Ce que l'agent verra » coupé.** Avec un élément large (ex. badge `span.badge` 161×26), la miniature est rognée à gauche dans sa case fixe. Quand l'image est plus large que haute au-delà d'un seuil, la pastille passe en disposition verticale : l'image en pleine largeur en haut, sans recadrage (`contain`), et Page / Élément / Style en dessous.

Go de l'utilisateur le 14 novembre 2026 (après les points 1 à 10) : lancer les modifications. Les points 1, 2, 3, 5, 6 et le glissé du bandeau partent en code directement. Les points 4, 8, 9 (dégradé), 10 et l'état « Lue par » sont d'abord designés dans Paper pour validation.

## Planches Paper (page « MCP - Design UI UX », section « 5 · Retours du test — à valider »)

| Planche | Point | Statut |
|---|---|---|
| 5.1 Entrée du mode, pic du dégradé (~180 ms) | 9 | à valider |
| 5.2 Bandeau vivant : vague tramée + chronologie d'entrée (bandeau 220 ms, dégradé 140/80/280 ms, lanceur 120→320 ms, vague 4 s) | 9, 10 | à valider ; le glissé du bandeau est déjà en code (V3-A) |
| 5.3 Espacements au survol (padding hachuré bleu, gap hachuré rose, une valeur par écart) | 8 | à valider |
| 5.4 Curseur : A « flèche + note » (recommandé) ou B « viseur » | 4 | choix à faire |
| 5.5 Suivi par l'agent : « Lue par Claude », « Claude n'a pas pu la traiter » + raison dans le suivi | 5 | référence pour V3-A |
| 5.6 Connecté : « Redémarrez Claude et Codex pour les activer » | 1 | référence pour V3-A |
| 5.7 Ce que l'agent verra, image large en disposition verticale | 11 | référence pour V3-A |
| 5.8 Hors mode : pas de lanceur, historique par Ctrl+Maj+H, ancré en bas à droite | 6 | référence pour V3-A |

Validation de l'utilisateur (14 novembre 2026, après correction de la vague) : 5.1 à 5.3 validées ; curseur **B « viseur »** sur tout le viewport en mode annotation (le coordinateur avait d'abord compris A ; corrigé après le test du preview 0.3.258). Également demandé après ce test : étiquettes de valeur des espacements dessinées devant la bordure du cadre, et plus de coupures aux jonctions des hachures. La vague ne passe jamais sous un texte (Mode Annotation, Quitter / Échap, rappel). Code : tâche V3-B, qui démarre après V3-A (mêmes fichiers).

## Pour une future mise à jour (pas dans V3)

- **Dicter la note à voix haute.** Pistes, de la plus simple à la plus ambitieuse :
  1. Dictée de Windows (Win+H) : indice dans le champ, ou bouton micro qui l'ouvre. Zéro dépendance, mais reconnaissance en ligne chez Microsoft.
  2. Reconnaissance vocale web dans la page : déconseillée, WebView2 ne la prend pas en charge de façon fiable.
  3. API de reconnaissance vocale de Windows (WinRT) : interface à nous, mais la dictée libre exige la « reconnaissance vocale en ligne ».
  4. **Whisper en local (whisper.cpp, MIT)** : 100 % sur la machine. Modèle téléchargé à la première activation (~140 Mo « base » / ~460 Mo « small »), rien de chargé tant qu'on n'appuie pas sur le micro.
  5. API cloud de transcription : écartée, l'audio quitterait la machine.

  Recommandation : 1 d'abord (un jour de travail), puis 4 en option si l'usage se confirme. Design Paper à faire le moment venu (bouton micro, état « en écoute », niveau sonore).

Contexte du test : la sandbox a été lancée avec `--agents real`. Les configs réelles contiennent donc l'entrée du preview si l'utilisateur a cliqué « Connecter ». Proposer « Déconnecter » ou un nettoyage à la fin des tests. L'ancienne donnée V1 du preview est rangée dans `build/annotations-preview/annotations.v1-old-20261114`.

## Chat V1.2 — retours du test réel sur la landing page (18 novembre 2026)

Preview 0.3.419, vrai Claude Code, `landing.html`. Lancés en code (tâche « Chat V1.2 ») :

1. Le texte tapé dans la note n'arrive pas à l'agent avec « Demander à l'agent » : seule la puce de l'élément part.
2. Effort de raisonnement : retirer « Default », Medium par défaut.
3. Retirer la puce dossier du composer : un chat = un seul projet (détection automatique et écran de choix gardés).
4. L'infobulle de la jauge de contexte ne se ferme pas quand la souris s'en va.
5. Le cadre posé sur l'élément (case cochée, et « Modifié par ») décale par rapport à la page au défilement.
6. Le curseur disparaît après avoir sélectionné un élément puis éloigné la souris.
7. Décision (corrigée après test) : un clic sur un élément ouvre **toujours** le chat à droite, avec l'élément en puce dans la saisie. Plus de petite boîte de note (sauf site non local). Annotation sans agent : Ctrl+Entrée dans la saisie du chat.

## Hors chat — à faire sur une branche à part (20 novembre 2026)

- **Infobulles centrées.** Dans le panneau média (« Sons et musiques », icône ⓘ), l'infobulle « Ce site peut jouer des sons… » part à droite de l'icône au lieu d'être centrée dessous. Règle pour **toutes** les infobulles au survol du navigateur : centrées horizontalement sur leur ancre autant que possible, décalées seulement pour rester dans la fenêtre / l'écran.
