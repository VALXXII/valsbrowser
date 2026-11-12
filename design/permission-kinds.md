# Demandes d'autorisation : types et textes

## Cause (capture x.com)

Le popover « x.com demande une autorisation » n'était pas une notification. Le moteur envoyait `kind=13`,
`COREWEBVIEW2_PERMISSION_KIND_PERSISTENT_STORAGE` dans le SDK 1.0.4314.35 (`navigator.storage.persist()`).
`OriginPrompt` ne connaissait que les types 0 à 12 et tombait sur le texte générique, sans explication.

## Contrat (Paper B02, état validé par l'utilisateur)

* Un type : glyphe, titre, une phrase d'explication ; case « Mémoriser pour ce site » ; Bloquer / Autoriser ;
  Fermer.
* Notifications : titre, « Le site pourra afficher des alertes en dehors de la page. », Bloquer / Autoriser,
  Fermer. Ni note ni case : le choix reste toujours mémorisé (politique inchangée). « Même onglet fermé » est
  retiré, faute de preuve dans la documentation Microsoft.
* Type 13 : « <site> veut protéger son stockage local » et « Protège les données locales du site quand le
  navigateur libère de l'espace. Les mots de passe sont gérés séparément. », avec un glyphe base de données
  (popover et puce). Ancienne copie « veut conserver ses données sur cet appareil » remplacée (Paper
  c-qdlkgz) : elle se lisait comme « garder mes identifiants ». Le type 13 (PERSISTENT_STORAGE) demande
  seulement que le stockage du site ne soit pas évincé quand le navigateur libère de l'espace ; ce n'est
  ni une notification ni un enregistrement de mot de passe (le coffre reste désactivé). La copie ne promet
  pas que le site ne stocke aucun élément de session : elle décrit la portée de l'autorisation, rien de plus.
* Type 0 : « Le moteur ne précise pas de quoi il s'agit. » ; type supérieur à 13 : « Type non reconnu par ce
  navigateur (code moteur n). » Le type n'est jamais deviné.
* Pas d'aide repliable : l'utilisateur l'a retirée de Paper.

## Preuves

* `test-popups.ps1 -SkipPixels` (`modal_test`) : textes par type ; éléments UIA exposés exactement ; ordre
  Tab ; une seule réponse par demande ; rendus à 96/144/192 DPI.
* `test-elements-integration.ps1` : types 13, 4, 0 et 14 routés avec de faux arguments WebView2 ; chaque
  réponse donnée une seule fois.
* `tests/e2e/permission_help_e2e.ps1` : vrai moteur sur une copie jetable du build, avec un profil neuf et un
  serveur local temporaire. Le moteur envoie bien `kind=13` et `kind=4`, et chaque demande est fermée une
  seule fois. Aucun clic « Autoriser ».
