# #121 — Liste sous le champ, liste réunie, bulle de vérification, pastille de la clé

Branche `VALXXII/121-autofill`. Planches Paper « #121 — Fenêtres du moteur à notre design » : « Infos enregistrées · A »,
« Liste réunie, comptes + infos · A », « Bulle de vérification · A », « Clé dans la barre d'adresse · A / B / C » (C).
Revue de sécurité (étape 0, avant le code) : `design/security-review-autofill-under-field.md` (P1–P16, aucun point à
trancher par Val).

## Ce qui est codé

| Planche | Code | Valeurs Paper reprises |
|---|---|---|
| Infos enregistrées · A | `src/field-list.h` (fenêtre), `src/forms-integration.h` (hôte), `src/form-memory.h` (données, script du monde isolé) | 320 de large, #171717, bord 1 px #FFFFFF14, rayon 10, marge 6, écart 2 ; titre 14/18 Medium 50 % ; ligne 36, enveloppe 16 à 60 %, texte 16/20 −0,01 em, lettres tapées SemiBold blanches, reste 70 %, coupé sans « … » ; croix 28×28 (x 12 à 70 %) au survol seulement ; filet #FFFFFF0D ; pied 32, curseurs 16, 14/18 à 70 % ; 8 DIP sous le champ, bords gauches alignés |
| Liste réunie · A | même fenêtre | comptes d'abord (ligne 48, pastille 24 #3E3E3E + initiale 12 SemiBold 80 %, identifiant 16/20, « •••••••••••• » 14/16 +0,08 em 50 %), second titre marge haute 10, une adresse déjà compte n'est pas répétée, pied « Gérer les mots de passe et la saisie » |
| Bulle de vérification · A | `src/validation-bubble.h` | #171717, bord #FFFFFF0F, rayon 10, marge 6/8, écart 8, icône warning-circle pleine 16 (#FF7B6E) dans 16×20, texte 14/20 à 90 %, 320 max, 6 DIP sous le champ, pas de flèche |
| Clé · variante C | `src/chrome.h` (`VaultPillBox`, `DrawVaultKey`) | pastille #FFFFFF14, rayon 4, hauteur 24, marges 6/7, écart 4, clé 14 et nombre Inter 13 Medium à 80 %, 2 DIP après le cadenas, l'adresse 8 DIP après ; la clé à gauche de « Copier le lien » est retirée |

Icônes : tracés exacts des planches (enveloppe, croix, curseurs, warning-circle, clé), pris avec `get_jsx`.

## Données « infos enregistrées »

Notre propre fichier `<profil>\form-entries` (e-mails tapés par la personne et envoyés). La saisie automatique du moteur
(`IsGeneralAutofillEnabled`) est éteinte partout pour qu'il n'y ait jamais deux listes. Rien n'est perdu : ses données
restent intactes sur le disque (jamais supprimées automatiquement) et, une seule fois, les e-mails de sa table `autofill`
(`Web Data`, jamais `Login Data`, rien de déchiffré) sont recopiés dans notre fichier, avant le démarrage du moteur
(réessayé au démarrage suivant s'il tient le fichier).

## États validés (planche « Listes sous le champ + bulle · états », 24 novembre) : codés

- Bulle : les 16 messages mot pour mot ; pas de ligne d'exemple grise (les exemples sont dans les messages : « par
  exemple gmail.com », « par exemple https://site.fr ») ; durée : tant que le champ n'est pas corrigé, 5 s au plus, part
  dès qu'on tape, quitte le champ, fait défiler ou change d'onglet.
- Liste : ↓ choisit une ligne (fond #333333 + touche « Entrée » #FFFFFF14, rayon 4, 12/16 Medium 70 %), Entrée remplit,
  Suppr retire l'e-mail ; pied survolé #333333 ; croix survolée #FFFFFF1A, appuyée #FFFFFF29 ; au-dessus du champ s'il
  n'y a pas la place, même écart de 8 ; au plus 4 comptes et 5 infos ; rien en navigation privée ni sous un champ mot de
  passe.
- Pastille : survolée #FFFFFF24, clé pleine et nombre en blanc ; avec une puce caméra / micro : la clé d'abord, la puce
  4 DIP après.
- Réglages › Confidentialité : « Propose les adresses e-mail que vous avez déjà tapées. »
- Par la clé, l'étape « identifiant seul » remplit maintenant l'identifiant du compte (sans mot de passe, rien de
  déchiffré), comme la liste réunie ; aucun nouveau message.

Écarts ou points encore ouverts :
- Textes anglais écrits par nous (la planche n'a que le français).
- Décisions de Val (24 novembre, nuit) : clé en contour (Phosphor Bold) au repos et au survol ; bulle 320 de large au
  plus ; textes anglais gardés ; trois messages de plus dans la même bulle : « Cette date n’est pas valide. », « Cette
  heure n’est pas valide. », « Choisissez une valeur. » (date, heure, couleur ou curseur obligatoire laissé vide). Seul
  le pas refusé d'un champ numérique (step) garde « Cette valeur n’est pas valide. ».
- Touche « Entrée » aussi sur une ligne de compte choisie au clavier (la planche ne montre qu'une ligne d'e-mail).
- « Effacer les données » (A5) n'a pas de « Tout effacer » : les e-mails partent avec l'effacement complet et à la fermeture.
- Règle A1 du propriétaire (survol plus sombre, anneau intérieur, contraste ≥ 4,5:1) : elle concerne les commandes
  pleines colorées ; les survols de la planche sont des voiles blancs sur fond sombre, aucun conflit relevé (texte
  #FFFFFFB3 sur #333333 ≈ 7:1).

## Pas dessiné sur les planches avant le 24 novembre (choix pris alors, la plupart dessinés depuis)

1. Bulle : la ligne d'exemple grise citée dans le commentaire #121 n'est pas sur la planche, donc pas affichée.
2. Bulle : durée (celle du moteur : 5 s, ou dès qu'on tape, quitte le champ, fait défiler, change d'onglet).
3. Bulle : les autres messages, écrits dans le ton de la planche (`form_*` du catalogue) : champ vide, case à cocher,
   choix, fichier, début ou fin d'adresse manquant, adresse invalide, adresse web, format, nombre, trop court / trop long,
   minimum / maximum ; message propre au site (`setCustomValidity`) affiché sur une ligne de 150 caractères au plus.
4. Liste : survol du pied « Gérer… » (pris : le fond des lignes, #333333).
5. Liste : ligne choisie au clavier (pris : l'aspect du survol, croix comprise).
6. Liste : croix « retirer » survolée ou appuyée (rien de plus que la planche).
7. Liste : comptes sans infos tapées (pris : titre B03 + pied « Gérer les mots de passe », qui ouvre B05).
8. Liste : ouverture au clic dans un champ vide (non : elle s'ouvre quand on tape) ; comptes filtrés par les lettres tapées
   (la planche ne les met pas en gras : pas de gras).
9. Liste : nom et adresse postale (pas d'icône dessinée : seuls les e-mails sont retenus et proposés).
10. Liste : sous un champ mot de passe (pas dessinée : la clé reste le chemin).
11. Liste : au plus 4 comptes et 5 infos, sans défilement ; au-dessus du champ quand elle ne tient pas dessous.
12. Fenêtre privée : pas de liste sous le champ (la clé reste, lecture seule).
13. Pastille : survol (pris : +4 % comme la puce « Non sécurisé ») ; avec une puce d'autorisation B01 en même temps (la
    pastille d'abord, la puce B01 ensuite).
14. Réglages › Confidentialité : la note « Remplit nom, adresse et e-mail. » ne correspond plus (seulement les e-mails) ;
    « Effacer les données » (A5) n'a pas de case « formulaires » (effacés avec l'effacement complet et à la fermeture).

## Limites du moteur (WebView2)

- Sites où le moteur garde un ancien mot de passe (option A) : sa propre liste peut encore s'ouvrir sous le champ jusqu'à
  la suppression proposée après 30 jours (R4).
- Cadres (iframes) : pas de liste ; la bulle du moteur reste dans les cadres.
- Fenêtres : coin découpé en région 1 bit (comme tous nos menus et bulles, `ConfigureShadowlessPopup`).
- Texte : lissage DirectWrite, quelques pixels différents de Paper (géométrie identique, voir les comparaisons).

## Vérifications

- Sans fenêtre (jour) : `test-form-memory.ps1` (82 contrôles + `node --check` du script + rendus hors écran comparés aux
  exports Paper de `tests/e2e/autofill-reference`), `test-elements-integration.ps1`, `tools/i18n/check_strings.py`,
  compilation complète et `main.cpp` en `-Werror`.
- Moteur réel sans écran (jour) : `python -I tests/forms-world/run.py` (fenêtre jamais affichée, 22 contrôles du script
  du monde isolé dans Chromium : frappe réelle seulement, cadres, champ caché, clavier, remplissage, enregistrement,
  bulle, page hostile).
- Avec fenêtre (nuit) : `tests/e2e/autofill_qa.ps1` (hôte produit + épingle de la fixture, pages `autofill_server.py`,
  captures comparées aux planches, pages hostiles T2–T9, pastille). Preuves dans `build\autofill-qa\`.

## État au départ de la ronde de nuit (24 novembre)

Fait le jour, sans fenêtre : compilation complète sans avertissement, `main.cpp` en `-Werror`, traductions OK,
`test-form-memory.ps1` (82 + comparaison Paper), `tests/forms-world/run.py` (22 dans le vrai moteur),
`test-elements-integration.ps1`, `run_all.py` (`existing`, `peers`, `integration` : PASS). Seul
`edit_flicker_test` (test d'image par image des champs de saisie) a échoué pendant la journée : il échoue aussi sur
`main` tant que d'autres agents compilent (`main` 0 à 42 échecs, la branche 15 à 46 selon la charge) : à repasser seul.

## Pour la ronde de nuit (branche `VALXXII/121-autofill`, machine calme, session déverrouillée)

1. `python tools/release/build.py --out build/v2-night --channel dev`
2. `build/update-integration-tests/edit_flicker_test.exe <racine> build/flicker` seul, puis la même chose sur `main` si
   un échec reste (comparer).
3. `powershell -ExecutionPolicy Bypass -File tests/e2e/autofill_qa.ps1` : liste, liste réunie, bulle, pastille comparées
   aux planches, pages hostiles ; preuves dans `build\autofill-qa\`.
4. Non-régression du coffre (la clé a changé de place, le remplissage B03 est passé par `VaultFillChosen`) :
   `tests/e2e/vault_product_e2e.ps1`, `tests/e2e/vault_e2e.ps1`, `tests/e2e/vault_coverage_e2e.ps1`,
   `tests/e2e/vault_review_e2e.ps1`.
5. Si tout passe : PR avec « Refs #121 », fusion avec les autres correctifs.

L'étape « identifiant seul » par la clé est réglée (24 novembre, `UserStepScript`).
États validés : `autofill_qa.ps1` capture aussi la ligne choisie au clavier, le pied et la croix survolés, trois bulles
de la planche des états, la liste au-dessus du champ et la pastille survolée (comparées à `tests/e2e/autofill-reference`).

## Nuit du 24 novembre : la clé absente en fenêtre privée (vault_e2e 34/37) — corrigé

Cause : une fenêtre privée démarre un moteur neuf ; sa première visite du site de test lève l'erreur de certificat
(acceptée par l'hôte de test), donc la barre affiche « Non sécurisé ». La pastille (variante C) se cachait dès qu'une
puce était affichée, alors que sur main la clé restait disponible. Les 3 contrôles R-2 de vault_e2e cherchent la clé
(« VaultFill ») dans la fenêtre privée : ils ne la trouvaient plus.
Correctif (`src/chrome.h`) : la pastille ne dépend plus de la puce ; elle suit le cadenas, ou la plaque « Non sécurisé »
quand elle est là. Seul cas où elle s'efface : pendant qu'on modifie l'adresse (le champ de saisie couvre cet endroit).
Test sans fenêtre qui l'aurait vu : `tests/form_memory_test.cpp` « KeyPill » (barre rendue hors écran, page sûre et page
« Non sécurisé », 96 et 144 DPI) ; il échoue avec l'ancien code, passe avec le correctif.
Script de nuit : `autofill_qa.ps1` section 7 (fenêtre privée neuve, la pastille doit être là) ; la recherche de la clé sur
l'étape identifiant attend maintenant que le navigateur ait relu le coffre (au plus toutes les 2 s, comme sur main).

## 24 novembre : la clé à côté de la pastille « Local » (#127, PR #139)

Avec #139, une adresse de cet ordinateur (localhost, 127.0.0.1, etc.) affiche « Local » au lieu de « Non sécurisé » ;
c'est aussi le cas des pages de test du coffre (https://127.0.0.1). La clé se plaçait déjà juste après « Local » (2 DIP,
comme après le cadenas), mais le trait de séparation et l'adresse de #139 partaient de « Local » : ils passaient sous la
clé. Adaptation dans cette branche (`src/chrome.h`, partie « Local » fusionnée) : quand la clé est là, le trait vient 6 DIP
après elle, puis l'adresse 6 DIP plus loin, comme sur la planche de #139. Le trait est posé sur un DIP entier (comme
« Local » arrondit sa largeur) pour rester net ; même chose après la puce « Non sécurisé ». Rien à changer dans #139.
Aucune planche ne montre la clé à côté de « Local » : l'écart de 2 DIP reprend celui du cadenas (point non dessiné).
Test sans fenêtre : `tests/form_memory_test.cpp` « KeyPill » (page sûre, « Non sécurisé », « Local » avec et sans erreur
de certificat, 96 et 144 DPI) : 8 contrôles échouent sans l'adaptation, tous passent avec.
