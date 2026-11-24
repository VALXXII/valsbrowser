// Fichier « source » simulé du bouton « Modifier le panier » (bac à sable du chat).
// La page réelle est index.html ; ce fichier sert à vérifier que fichier:ligne (données de debug React)
// pointe sur un fichier qui existe dans le dossier du projet.
export function CartButton({ onEdit }) {
  return (
    <button className="btn-ghost" type="button" onClick={onEdit}>
      Modifier le panier
    </button>
  );
}
