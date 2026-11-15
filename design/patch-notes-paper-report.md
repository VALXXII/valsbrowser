# Notes de version — maquette Paper

Demande : une petite modale présentant rapidement les ajouts et correctifs, avec le design system existant.

Lien : https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-L-0

Deux maquettes éditables, ajoutées uniquement sur la nouvelle page « Notes de version — modale » :
- `12VC-0` : français ; `12WI-0` : anglais, à 80 px de la première.
- Largeur 420 px, hauteur au contenu ; surface, bordure, rayon 10 px, Inter Display 16/20 et 14/20, padding/gap 16 px repris des modales existantes.
- Footer cloné du composant A1 (`DF7-0`), boutons 32 px, rayon 6 px : « Notes complètes » et « Fermer » / « Full release notes » et « Close ».

Contenu d'aperçu, sous-titré « Prochaine mise à jour » / « Upcoming update », sans numéro de version inventé :
- choix français / anglais dans les Réglages ;
- sélection de l'adresse aux couleurs du profil ;
- changement d'onglet sans flou ;
- barre du navigateur masquée en plein écran ;
- renommage des dossiers de favoris sur place.

Il s'agit du contenu proposé pour la prochaine livraison, pas d'une déclaration que ces cinq points sont tous publiés. Le plein écran et le renommage sont encore en validation au moment du dessin. Les paiements ne sont pas annoncés comme réparés. Le contenu et la version devront correspondre à la livraison réelle avant intégration dans l'application.

Vérification : captures réellement relues après Ajouts, Correctifs, footer FR, traduction EN et footer EN. Lisibilité, retraits des cinq puces, rythme entre groupes, alignement des actions et absence de coupure vérifiés ; hauteur passée à fit-content. Police Inter Display et graisses disponibles vérifiées avant écriture. Pas de nouvelle palette, de token global ni d'illustration.

Préservation : seules les deux nouvelles racines et leurs descendants sur p-L-0 ont été écrits. Référence p-1-0 inchangée en nombre (165 planches / 18047 nœuds), projet protégé p-H-0 (35 / 1832), Icon p-C-0 (6 / 31). Tokens b37eaa4f et les 12 fils de commentaires sont strictement identiques dans les réponses avant/après. Aucun déplacement, renommage ou suppression de contenu existant ; ordre relatif des anciennes pages conservé. Ces constats valent à la fin de ce lot, sans attribuer de futures retouches utilisateur.

`finish_working_on_nodes` appelé, slot libéré. Design seulement : aucun mécanisme d'affichage après mise à jour, lien actif, mémorisation de lecture ou nouvelle modale de production n'a été codé par ce lot.
